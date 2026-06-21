// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "RPG_Objective.generated.h"

class URPG_ObjectiveTrackerComponent;

/**
 * Inline objective evaluator: the "HOW does this objective make progress" half of a quest stage.
 *
 * Authored inline (EditInlineNew, Instanced) inside FRPG_QuestStageObjective. When the tracker activates a
 * stage it calls BeginTracking on each objective's evaluator; the evaluator subscribes to the message bus
 * (DP.Bus.* via UDP_MessageBusSubsystem::ListenNative) or to a hub counter, and on each relevant event it
 * calls the tracker's ReportProgress with a delta. EndTracking unsubscribes.
 *
 * Genre-agnostic and RPG-OWNED: evaluators couple to other systems only through the message bus and the
 * shared read seams (ISeam_ItemQuery), never by including another module's concrete header. (_TalkToNpc, for
 * example, listens to the DP.Bus.Narrative.DialogueFinished channel — bus coupling, not header coupling.)
 *
 * Evaluators run AUTHORITY-SIDE (BeginTracking is only called on the server by the tracker), so they never
 * mutate replicated state directly — they hand deltas to the tracker, which routes them into the
 * authority-guarded URPG_QuestLogComponent::AdvanceObjective.
 */
UCLASS(Abstract, EditInlineNew, BlueprintType, DefaultToInstanced, CollapseCategories)
class DESIGNPATTERNSRPG_API URPG_Objective : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Begin observing for progress. Called AUTHORITY-side by the tracker when the owning stage activates.
	 * Default caches the tracker/quest/objective ids; subclasses call Super then subscribe to their source.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "RPG|Quest|Objective")
	void BeginTracking(URPG_ObjectiveTrackerComponent* Tracker, FGameplayTag QuestTag, FGameplayTag ObjectiveTag);
	virtual void BeginTracking_Implementation(URPG_ObjectiveTrackerComponent* Tracker, FGameplayTag QuestTag, FGameplayTag ObjectiveTag);

	/** Stop observing (unsubscribe). Default clears the cached tracker; subclasses release subscriptions. */
	UFUNCTION(BlueprintNativeEvent, Category = "RPG|Quest|Objective")
	void EndTracking();
	virtual void EndTracking_Implementation();

	/**
	 * @return the current authoritative progress count this evaluator can directly observe (e.g. a hub
	 * counter or an inventory item count), or INDEX_NONE when progress is purely event-driven and only the
	 * tracker's accumulated counter is meaningful. Used to seed counters on stage (re)activation / restore.
	 */
	virtual int32 QueryCurrentProgress(const URPG_ObjectiveTrackerComponent* Tracker) const { return INDEX_NONE; }

	/** Human-readable summary for debug/editor tooltips. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Quest|Objective")
	virtual FText DescribeObjective() const;

	/** Identity of the objective this evaluator drives (matches the wrapped FRPG_QuestObjective::ObjectiveTag). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Quest|Objective")
	FGameplayTag ObjectiveTag;

	/** The gameplay subject this objective targets (an enemy archetype, item, location, npc, ...). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Quest|Objective")
	FGameplayTag TargetTag;

	/**
	 * The bus channel this objective listens on. When unset a subclass uses its conventional default
	 * channel root. Set to a more specific channel to scope an objective to a particular event source.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Quest|Objective", meta = (Categories = "DP.Bus"))
	FGameplayTag BusChannel;

protected:
	/** Non-owning back-pointer to the tracker driving this evaluator (valid between Begin/EndTracking). */
	UPROPERTY(Transient)
	TWeakObjectPtr<URPG_ObjectiveTrackerComponent> OwningTracker;

	/** The quest this evaluator's objective belongs to (cached on BeginTracking). */
	UPROPERTY(Transient)
	FGameplayTag CachedQuestTag;

	/** Convenience: report Delta units of progress for this objective to the tracker (authority-side). */
	void ReportToTracker(int32 Delta);

	/** Resolve the message bus from the tracker's world context (null-safe). */
	class UDP_MessageBusSubsystem* GetBus() const;
};
