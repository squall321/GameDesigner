// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "UObject/ScriptInterface.h"
#include "MessageBus/DPMessage.h"
#include "Persist/Seam_Persistable.h"
#include "Tutorial/Tut_Condition.h"
#include "Tutorial/Tut_TutorialTypes.h"

// FInstancedStruct lives in StructUtils on UE 5.3/5.4, merged into CoreUObject in 5.5. Included BEFORE the
// generated header (a version-gated include must precede the .generated.h, per the module rules).
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "Tut_TutorialSubsystem.generated.h"

class UTut_TutorialDefinition;
class UTut_TutorialViewModel;

/**
 * Broadcast when the active tutorial's step (or active/inactive status) changes — a native hook for code that
 * wants tutorial progress without binding the ViewModel or the bus.
 * @param TutorialTag the active tutorial's DataTag (invalid when none).
 * @param StepIndex   zero-based active step index (INDEX_NONE when inactive/completed).
 */
DECLARE_MULTICAST_DELEGATE_TwoParams(FTut_OnTutorialStepChanged, FGameplayTag /*TutorialTag*/, int32 /*StepIndex*/);

/**
 * Runs a data-driven tutorial sequence — the heart of the module.
 *
 * GameInstance-scoped and LOCAL/COSMETIC: it never replicates state and never mutates authoritative gameplay.
 * It drives a UTut_TutorialDefinition's steps by:
 *  - listening on the message bus (ListenNative) for one-shot trigger/completion events, recording seen event
 *    tags so UTut_Condition_BusEvent conditions report them;
 *  - re-evaluating the current step's Trigger (to surface it) and Completion (to advance) as events arrive;
 *  - surfacing the active step through a UTut_TutorialViewModel the UI binds to;
 *  - highlighting a UI target via the resolved ISeam_UIHighlight seam (degrades to no-op if unresolved);
 *  - gating input via the resolved ISeam_InputModeArbiter seam while a step requests it (degrades gracefully);
 *  - persisting the set of completed tutorials via ISeam_Persistable so completed tutorials never replay.
 *
 * It also implements ITut_ConditionContext so its conditions can ask it about seen bus events and read the
 * world-hub seam — keeping conditions free of any subsystem coupling.
 *
 * This subsystem holds the world-hub read seam WEAKLY (a TScriptInterface kept only while live and re-resolved
 * on demand) — it never hard-roots a cross-world interface.
 */
UCLASS()
class DESIGNPATTERNSTUTORIAL_API UTut_TutorialSubsystem
	: public UDP_GameInstanceSubsystem
	, public ISeam_Persistable
	, public ITut_ConditionContext
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	// ---- Public API ----

	/**
	 * Start the tutorial identified by TutorialTag (a UTut_TutorialDefinition DataTag in the data registry).
	 * No-ops (returns false) if the tutorial is already completed/suppressed, another tutorial is running, or
	 * the definition cannot be resolved.
	 * @return true if the tutorial started.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tutorial")
	bool StartTutorial(FGameplayTag TutorialTag);

	/**
	 * Skip the active tutorial (or the named one if it is the active one), marking it completed (so it does
	 * not replay when bSkipCountsAsCompleted is set) and tearing down highlight/input gating.
	 * @return true if a tutorial was skipped.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tutorial")
	bool SkipTutorial();

	/** True if a tutorial is currently running. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Tutorial")
	bool IsTutorialActive() const { return ActiveDefinition != nullptr; }

	/** The active tutorial's DataTag (invalid when none). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Tutorial")
	FGameplayTag GetActiveTutorialTag() const { return ActiveTutorialTag; }

	/** True if TutorialTag has been completed (or skipped, when skip counts as completed) this profile. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Tutorial")
	bool IsTutorialCompleted(FGameplayTag TutorialTag) const;

	/** The ViewModel the tutorial UI binds to. Stable for the subsystem lifetime; never null after Initialize. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Tutorial")
	UTut_TutorialViewModel* GetViewModel() const { return ViewModel; }

	/** Native step-changed hook for code that does not bind the ViewModel. */
	FTut_OnTutorialStepChanged OnTutorialStepChanged;

	//~ Begin ISeam_Persistable
	/** Capture the completed-tutorial set as an FTut_TutorialSaveRecord. */
	virtual void CaptureState_Implementation(FInstancedStruct& Out) const override;
	/** Restore the completed-tutorial set; LOCAL state only, so no authority guard is required here. */
	virtual void RestoreState_Implementation(const FInstancedStruct& In) override;
	/** The record-kind tag this participant saves under (TutTags::Persist_Kind_Tutorial). */
	virtual FGameplayTag GetPersistenceKind_Implementation() const override;
	//~ End ISeam_Persistable

	//~ Begin ITut_ConditionContext
	virtual bool HasSeenBusEvent(const FGameplayTag& EventTag) const override;
	virtual bool QueryHubValue(const FGameplayTag& Key, FInstancedStruct& Out) const override;
	//~ End ITut_ConditionContext

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

private:
	// ---- Step lifecycle ----

	/** Enter step Index: arm its conditions, surface (if trigger already met) and push to the ViewModel. */
	void EnterStep(int32 Index);

	/** Re-evaluate the active step: surface it when its trigger is met, advance when its completion is met. */
	void EvaluateActiveStep();

	/** Advance to the next step, or complete the tutorial if the last step finished. */
	void AdvanceStep();

	/** Finish the active tutorial (completed or skipped): record completion, tear down, broadcast, clear VM. */
	void FinishTutorial(bool bSkipped);

	/** True once the active step's trigger has fired and the instruction is on screen. */
	bool bActiveStepSurfaced = false;

	// ---- Highlight / input gating (seam-driven, degrade to no-op when unresolved) ----

	/** Begin highlighting the active step's target (if any) through the ISeam_UIHighlight seam. */
	void ApplyStepHighlight(const FTut_TutorialStep& Step);

	/** Clear any highlight applied for the active step. */
	void ClearStepHighlight();

	/** Push the active step's input mode (if it gates input) through ISeam_InputModeArbiter. */
	void ApplyStepInputGate(const FTut_TutorialStep& Step);

	/** Release any input-mode lock acquired for the active step. */
	void ReleaseStepInputGate();

	// ---- Seam resolution helpers (resolve fresh each time; never hard-root) ----

	/** Resolve the ISeam_UIHighlight provider object from the service locator, or null. */
	UObject* ResolveUIHighlight() const;

	/** Resolve the ISeam_InputModeArbiter provider object from the service locator, or null. */
	UObject* ResolveInputArbiter() const;

	/** Resolve the IWorldHub_Queryable provider object from the service locator, or null. */
	UObject* ResolveWorldHub() const;

	// ---- Bus + analytics ----

	/** Subscribe the broad bus listener used to feed UTut_Condition_BusEvent + trigger re-evaluation. */
	void RegisterBusListeners();

	/** Handle any bus message: record its channel as a seen one-shot event and re-evaluate the active step. */
	void HandleBusMessage(const FDP_Message& Message);

	/** Broadcast an FTut_TutorialEvent on the given channel for external listeners. */
	void BroadcastTutorialEvent(FGameplayTag Channel, ETut_TutorialStatus Status) const;

	/** Record a completion/skip analytics event through the ISeam_AnalyticsSink seam (if present). */
	void RecordAnalytics(FGameplayTag EventTag, int32 StepReached) const;

	/** Apply verbose-logging from settings. */
	void ApplySettings();

	// ---- State ----

	/** The active tutorial definition (strong while running so a content unload can't pull it out). */
	UPROPERTY(Transient)
	TObjectPtr<UTut_TutorialDefinition> ActiveDefinition = nullptr;

	/** DataTag of the active tutorial (mirrors ActiveDefinition->DataTag for fast access). */
	UPROPERTY(Transient)
	FGameplayTag ActiveTutorialTag;

	/** Zero-based index of the active step (INDEX_NONE when no tutorial running). */
	UPROPERTY(Transient)
	int32 ActiveStepIndex = INDEX_NONE;

	/** The ViewModel the UI binds to (instanced subobject, kept alive for the subsystem lifetime). */
	UPROPERTY(Transient)
	TObjectPtr<UTut_TutorialViewModel> ViewModel = nullptr;

	/** Completed (and, per settings, skipped) tutorial DataTags — the persisted set. */
	UPROPERTY(Transient)
	FGameplayTagContainer CompletedTutorials;

	/**
	 * One-shot bus event channels seen since the current step was armed (cleared on each step entry). Drives
	 * UTut_Condition_BusEvent. Not a UPROPERTY (plain tag container is fine; holds no UObject refs).
	 */
	FGameplayTagContainer SeenEventTags;

	/** The opaque input-mode request id held while the active step gates input (invalid when none). */
	FGuid InputModeRequestId;

	/** True while this subsystem holds an input-mode lock for the active step. */
	bool bHoldingInputMode = false;

	/** The highlight target currently applied (invalid when none), so it can be cleared exactly. */
	FGameplayTag ActiveHighlightTarget;
};
