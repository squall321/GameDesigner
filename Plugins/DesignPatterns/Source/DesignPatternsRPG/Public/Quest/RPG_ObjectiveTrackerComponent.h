// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "GameplayTagContainer.h"
#include "RPG_ObjectiveTrackerComponent.generated.h"

class URPG_ObjectiveTrackerComponent;
class URPG_QuestLogComponent;
class URPG_QuestGraphDefinition;
class URPG_Objective;
struct FRPG_QuestStage;
struct FRPG_QuestBranchOutcome;
struct FRPG_StageGate;
struct FRPG_HubWrite;

/**
 * Replicated per-quest stage state for the branching layer.
 *
 * Modeled EXACTLY on FRPG_InventoryEntry / FWorldHub_RepEntry: a fast-array item whose
 * PostReplicatedAdd/Change/PreReplicatedRemove fire the tracker's OnStageAdvanced via a NotReplicated
 * back-pointer on the owning array, so clients drive quest UI from delta-replicated stage cursors without a
 * separate OnRep. The canonical authoritative state (which stage, which hidden objectives revealed, when
 * the stage started) lives here on the server and replicates as deltas.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSRPG_API FRPG_QuestStageState : public FFastArraySerializerItem
{
	GENERATED_BODY()

	/** The quest this stage state belongs to (matches the quest definition's DataTag). */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "RPG|Quest")
	FGameplayTag QuestTag;

	/** The quest's current stage. */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "RPG|Quest")
	FGameplayTag CurrentStage;

	/** Hidden objectives that have been revealed to the UI (objective tags). */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "RPG|Quest")
	FGameplayTagContainer RevealedHiddenObjectives;

	/** Stages already visited (for FRPG_StageGate::RequiredPriorStage prerequisite checks). */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "RPG|Quest")
	FGameplayTagContainer VisitedStages;

	/**
	 * World time (UWorld::GetTimeSeconds) at which CurrentStage was entered. Used to evaluate the per-stage
	 * time limit. Persisted as a relative remaining-time on save so it is wall-clock-stable across reload.
	 */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "RPG|Quest")
	double StageStartWorldTime = 0.0;

	/** World time at which the WHOLE quest was accepted (for the overall quest time limit). */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "RPG|Quest")
	double QuestStartWorldTime = 0.0;

	//~ FFastArraySerializerItem replication callbacks (clients only).
	void PreReplicatedRemove(const struct FRPG_QuestStageStateArray& InArraySerializer);
	void PostReplicatedAdd(const struct FRPG_QuestStageStateArray& InArraySerializer);
	void PostReplicatedChange(const struct FRPG_QuestStageStateArray& InArraySerializer);
};

/**
 * Fast-array serializer holding the tracker's per-quest stage states.
 *
 * Plain UPROPERTY(Replicated) on the component (NOT ReplicatedUsing): change notification is delivered by
 * the per-item callbacks above through OwnerComponent, exactly like FRPG_InventoryArray. NetDeltaSerialize
 * forwards to FastArrayDeltaSerialize so only changed stage entries cross the wire.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSRPG_API FRPG_QuestStageStateArray : public FFastArraySerializer
{
	GENERATED_BODY()

	/** Replicated per-quest stage entries. */
	UPROPERTY(BlueprintReadOnly, Category = "RPG|Quest")
	TArray<FRPG_QuestStageState> Entries;

	/** Non-replicated back-pointer to the owning tracker, for change notifications. */
	UPROPERTY(NotReplicated, Transient)
	TObjectPtr<URPG_ObjectiveTrackerComponent> OwnerComponent = nullptr;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FastArrayDeltaSerialize<FRPG_QuestStageState, FRPG_QuestStageStateArray>(Entries, DeltaParms, *this);
	}
};

/** Enables NetDeltaSerialize for the stage-state array. */
template<>
struct TStructOpsTypeTraits<FRPG_QuestStageStateArray> : public TStructOpsTypeTraitsBase2<FRPG_QuestStageStateArray>
{
	enum { WithNetDeltaSerializer = true };
};

/** Broadcast (server + clients) when a quest's current stage changes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FRPG_OnStageAdvanced, FGameplayTag, QuestTag, FGameplayTag, NewStage);

/**
 * Server-authoritative bridge that drives a branching quest graph on top of the real URPG_QuestLogComponent.
 *
 * Responsibilities:
 *  - ActivateQuestGraph: validate the quest's AcceptGates, start it on the log, enter StartStage.
 *  - Activate ONLY the current stage's REQUIRED objectives into the log (so the base log's auto-complete
 *    fires per-stage, never mid-graph) and call BeginTracking on each objective's inline evaluator so they
 *    observe the bus/hub. EndTracking the previous stage's evaluators on advance.
 *  - ReportProgress: funnel an evaluator's delta into URPG_QuestLogComponent::AdvanceObjective, then
 *    re-evaluate stage completion (AND/OR) and branch outcomes.
 *  - On stage completion, evaluate FRPG_QuestBranchOutcome gates in order; the first passing outcome applies
 *    its FRPG_HubWrite effects (through the concrete world hub, authority-only) and routes to NextStage /
 *    completes / fails.
 *  - Run authority-side time limits anchored to the saved StageStartWorldTime / QuestStartWorldTime (a
 *    polling tick rather than a live FTimerHandle, so limits survive save/reload).
 *
 * Every mutator guards authority at the TOP. The component holds the replicated FRPG_QuestStageStateArray;
 * clients observe stage changes through it and the OnStageAdvanced delegate.
 */
UCLASS(ClassGroup = (DesignPatternsRPG), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSRPG_API URPG_ObjectiveTrackerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URPG_ObjectiveTrackerComponent();

	//~ Begin UActorComponent
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent

	/**
	 * Begin driving QuestTag as a branching graph. AUTHORITY ONLY. Resolves the quest definition (must be a
	 * URPG_QuestGraphDefinition), evaluates AcceptGates, starts the quest on the log, and enters StartStage.
	 * For a non-branching definition it is a no-op (the base log handles linear quests). @return true on
	 * successful activation.
	 */
	UFUNCTION(BlueprintCallable, Category = "RPG|Quest|Graph")
	bool ActivateQuestGraph(FGameplayTag QuestTag);

	/**
	 * Report Delta units of progress for an objective. AUTHORITY ONLY. Routes into the log's
	 * AdvanceObjective, then re-evaluates the current stage. Called by objective evaluators and by gameplay.
	 */
	UFUNCTION(BlueprintCallable, Category = "RPG|Quest|Graph")
	void ReportProgress(FGameplayTag QuestTag, FGameplayTag ObjectiveTag, int32 Delta = 1);

	/**
	 * Set an objective's progress to an ABSOLUTE value. AUTHORITY ONLY. Used by state-derived objectives
	 * (e.g. CollectItem) so that losing items regresses progress. Re-evaluates the current stage.
	 */
	UFUNCTION(BlueprintCallable, Category = "RPG|Quest|Graph")
	void SetObjectiveProgress(FGameplayTag QuestTag, FGameplayTag ObjectiveTag, int32 AbsoluteCount);

	/** Reveal a hidden objective to the tracker UI. AUTHORITY ONLY. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Quest|Graph")
	void RevealHiddenObjective(FGameplayTag QuestTag, FGameplayTag ObjectiveTag);

	/** Request that an in-progress quest fail (e.g. an escort target lost). AUTHORITY ONLY. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Quest|Graph")
	void RequestFailQuest(FGameplayTag QuestTag);

	/** @return the current stage of QuestTag, or an invalid tag if the quest is not being graph-tracked. */
	UFUNCTION(BlueprintPure, Category = "RPG|Quest|Graph")
	FGameplayTag GetCurrentStage(FGameplayTag QuestTag) const;

	/** @return true if ObjectiveTag is hidden in QuestTag's current stage AND not yet revealed. */
	UFUNCTION(BlueprintPure, Category = "RPG|Quest|Graph")
	bool IsObjectiveHidden(FGameplayTag QuestTag, FGameplayTag ObjectiveTag) const;

	/** @return seconds remaining on the current stage's time limit, or -1 if the stage has no limit. */
	UFUNCTION(BlueprintPure, Category = "RPG|Quest|Graph")
	float GetStageTimeRemaining(FGameplayTag QuestTag) const;

	/** The quest log this tracker drives (resolved off the owner if not set in the editor). */
	UFUNCTION(BlueprintPure, Category = "RPG|Quest|Graph")
	URPG_QuestLogComponent* GetQuestLog() const;

	/** Broadcast (server + clients) when a quest's current stage changes. */
	UPROPERTY(BlueprintAssignable, Category = "RPG|Quest|Graph")
	FRPG_OnStageAdvanced OnStageAdvanced;

	/** Called by the fast-array entry callbacks on clients to surface a stage change. */
	void HandleStageReplicated(const FGameplayTag& QuestTag, const FGameplayTag& NewStage);

	// ---- Save integration (called by URPG_QuestGraphSaveGame) -----------------------------------

	/** Snapshot the current stage states for persistence (server side). */
	TArray<FRPG_QuestStageState> ExportStageStates() const;

	/**
	 * Restore stage states from a save (server side). AUTHORITY ONLY. Re-anchors the world-time fields to
	 * "now" minus the saved elapsed so time limits resume correctly, and re-activates each restored stage's
	 * objective evaluators.
	 */
	void ImportStageStates(const TArray<FRPG_QuestStageState>& InStates);

private:
	/** Replicated per-quest stage states. */
	UPROPERTY(Replicated)
	FRPG_QuestStageStateArray StageStates;

	/** Optional explicit quest log; resolved off the owner when null. */
	UPROPERTY()
	TObjectPtr<URPG_QuestLogComponent> QuestLog;

	/** The inline objective evaluators currently active for the current stage (authority side). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<URPG_Objective>> ActiveObjectives;

	/** True if a polling time-limit tick is needed (any tracked quest/stage has a limit). */
	bool bAnyTimeLimit = false;

	// ---- Internals (all authority-side) ---------------------------------------------------------

	/** True on server / standalone / listen host. */
	bool HasAuthoritySafe() const;

	/** Find the stage-state entry index for QuestTag, or INDEX_NONE. */
	int32 FindStateIndex(const FGameplayTag& QuestTag) const;

	/** Resolve a quest's branching definition from the data registry, or null (non-branching/unknown). */
	URPG_QuestGraphDefinition* ResolveGraph(const FGameplayTag& QuestTag) const;

	/** Enter NewStage of QuestTag: end old evaluators, activate the new stage's objectives, anchor time. */
	void EnterStage(const FGameplayTag& QuestTag, const FGameplayTag& NewStage);

	/** Tear down the current stage's objective evaluators (EndTracking each). */
	void DeactivateObjectives();

	/** Activate StageDef's required objectives into the log and begin their evaluators (authority). */
	void ActivateStageObjectives(const FGameplayTag& QuestTag, const FRPG_QuestStage& StageDef);

	/** Re-evaluate the current stage's AND/OR completion + branch outcomes after a progress change. */
	void EvaluateStage(const FGameplayTag& QuestTag);

	/** @return true if StageDef is complete given the log's per-objective counters (honours AND/OR + optional). */
	bool IsStageComplete(const FGameplayTag& QuestTag, const FRPG_QuestStage& StageDef) const;

	/** Evaluate Outcomes in order; fire the first whose gate passes. @return true if an outcome fired. */
	bool TryFireOutcome(const FGameplayTag& QuestTag, const FRPG_QuestStage& StageDef);

	/** Apply a fired outcome (effects + route/complete/fail). */
	void ApplyOutcome(const FGameplayTag& QuestTag, const FRPG_QuestBranchOutcome& Outcome);

	/** Evaluate an RPG-local gate against the hub + reputation seam (fail-closed). */
	bool EvaluateGate(const FGameplayTag& QuestTag, const FRPG_StageGate& Gate) const;

	/** Apply one authoritative hub write (flag/counter) via the concrete hub. */
	void ApplyHubWrite(const FRPG_HubWrite& Write) const;

	/** Mark a state entry dirty for replication and notify locally. */
	void MarkStateDirtyAndNotify(FRPG_QuestStageState& Entry, bool bStageChanged);

	/** Broadcast an observer-only quest bus event (no-op if no bus). */
	void BroadcastQuestEvent(const FGameplayTag& Channel, const FGameplayTag& QuestTag, const FGameplayTag& NodeTag, int32 Value) const;

	/** Resolve the owner's net/save-stable entity id via the identity seam (invalid if none). */
	FGuid ResolveOwnerEntityGuid() const;

	/** Poll stage / quest time limits and trigger fail/fail-to routing on elapse (authority tick). */
	void TickTimeLimits();
};
