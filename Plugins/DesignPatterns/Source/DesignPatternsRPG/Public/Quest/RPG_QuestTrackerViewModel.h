// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "Quest/RPG_QuestLogComponent.h"   // ERPG_QuestState
#include "RPG_QuestTrackerViewModel.generated.h"

class URPG_QuestLogComponent;
class URPG_ObjectiveTrackerComponent;
class URPG_QuestGraphDefinition;

/**
 * A flattened, render-ready view of one tracked objective for the HUD tracker widget.
 *
 * Built locally from the (replicated) quest log counters + (replicated) tracker stage state. Hidden,
 * not-yet-revealed objectives are excluded from the list the VM exposes.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSRPG_API FRPG_TrackedObjectiveView
{
	GENERATED_BODY()

	/** Identity of the objective. */
	UPROPERTY(BlueprintReadOnly, Category = "RPG|Quest|VM")
	FGameplayTag ObjectiveTag;

	/** Player-facing description (from the wrapped FRPG_QuestObjective). */
	UPROPERTY(BlueprintReadOnly, Category = "RPG|Quest|VM")
	FText Description;

	/** Current progress count. */
	UPROPERTY(BlueprintReadOnly, Category = "RPG|Quest|VM")
	int32 Current = 0;

	/** Required count for completion. */
	UPROPERTY(BlueprintReadOnly, Category = "RPG|Quest|VM")
	int32 Required = 1;

	/** True if this is a bonus/optional objective. */
	UPROPERTY(BlueprintReadOnly, Category = "RPG|Quest|VM")
	bool bOptional = false;

	/** True if Current >= Required. */
	UPROPERTY(BlueprintReadOnly, Category = "RPG|Quest|VM")
	bool bComplete = false;
};

/** Fired locally whenever the tracked view changes, so the widget can re-pull GetTrackedObjectives. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRPG_OnTrackerRefreshed);

/**
 * Local-only HUD view-model for the quest tracker.
 *
 * Binds to the REAL FRPG_OnQuestStateChanged / FRPG_OnObjectiveProgress delegates on the quest log and the
 * OnStageAdvanced delegate on the objective tracker; on any of them it re-derives the visible objective set
 * (excluding hidden+unrevealed ones) for the currently-tracked quest and raises OnTrackerRefreshed. Never
 * replicated — it reads already-replicated state on whatever machine it lives on.
 */
UCLASS(BlueprintType, Blueprintable, Transient)
class DESIGNPATTERNSRPG_API URPG_QuestTrackerViewModel : public UObject
{
	GENERATED_BODY()

public:
	/** Bind to a quest log + objective tracker. Safe to call again to rebind. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Quest|VM")
	void Bind(URPG_QuestLogComponent* InLog, URPG_ObjectiveTrackerComponent* InTracker);

	/** Unbind from all delegates. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Quest|VM")
	void Unbind();

	/** Choose which quest the tracker UI is focused on (otherwise the first active quest is used). */
	UFUNCTION(BlueprintCallable, Category = "RPG|Quest|VM")
	void SetTrackedQuest(FGameplayTag QuestTag);

	/** @return the quest currently focused (the explicit one, or the first active quest). */
	UFUNCTION(BlueprintPure, Category = "RPG|Quest|VM")
	FGameplayTag GetTrackedQuest() const;

	/** @return the current stage tag of the tracked quest (invalid if linear / untracked). */
	UFUNCTION(BlueprintPure, Category = "RPG|Quest|VM")
	FGameplayTag GetTrackedStage() const;

	/** @return the player-facing title for the tracked quest's current stage. */
	UFUNCTION(BlueprintPure, Category = "RPG|Quest|VM")
	FText GetTrackedStageTitle() const;

	/** @return the visible objectives of the tracked quest's current stage. */
	UFUNCTION(BlueprintPure, Category = "RPG|Quest|VM")
	TArray<FRPG_TrackedObjectiveView> GetTrackedObjectives() const;

	/** @return seconds remaining on the tracked stage's time limit, or -1 if none. */
	UFUNCTION(BlueprintPure, Category = "RPG|Quest|VM")
	float GetStageTimeRemaining() const;

	/** Raised whenever the tracked view changes. */
	UPROPERTY(BlueprintAssignable, Category = "RPG|Quest|VM")
	FRPG_OnTrackerRefreshed OnTrackerRefreshed;

private:
	/** The quest log (read-only source of objective counters). Non-owning. */
	UPROPERTY(Transient)
	TWeakObjectPtr<URPG_QuestLogComponent> Log;

	/** The objective tracker (read-only source of stage state). Non-owning. */
	UPROPERTY(Transient)
	TWeakObjectPtr<URPG_ObjectiveTrackerComponent> Tracker;

	/** Explicitly-tracked quest; invalid means "first active quest". */
	FGameplayTag ExplicitQuest;

	/** Resolve the branching definition for the tracked quest, or null (linear). */
	URPG_QuestGraphDefinition* ResolveGraph(const FGameplayTag& QuestTag) const;

	//~ Delegate handlers (re-raise OnTrackerRefreshed) -------------------------------------------
	UFUNCTION()
	void HandleQuestStateChanged(URPG_QuestLogComponent* QuestLog, FGameplayTag QuestTag, ERPG_QuestState NewState);

	UFUNCTION()
	void HandleObjectiveProgress(URPG_QuestLogComponent* QuestLog, FGameplayTag QuestTag, FGameplayTag ObjectiveTag, int32 NewCount);

	UFUNCTION()
	void HandleStageAdvanced(FGameplayTag QuestTag, FGameplayTag NewStage);
};
