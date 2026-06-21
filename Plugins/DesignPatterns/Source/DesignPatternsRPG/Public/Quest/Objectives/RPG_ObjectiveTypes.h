// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Quest/Objectives/RPG_Objective.h"
#include "MessageBus/DPMessage.h"   // FDP_ListenerHandle (stored per evaluator)
#include "RPG_ObjectiveTypes.generated.h"

/**
 * Kill objective: counts bus "kill" events whose payload secondary tag matches TargetTag.
 *
 * Listens on BusChannel (default DP.Bus.RPG.Objective.Kill). The project broadcasts an FRPG_QuestBusEvent
 * on that channel when a relevant enemy dies; this evaluator filters by TargetTag (the slain archetype) and
 * reports +IntValue (default 1) progress. The "N" of "kill N" is the wrapped FRPG_QuestObjective's
 * RequiredCount on the stage, so no extra count field is needed here.
 */
UCLASS(meta = (DisplayName = "Kill N (Tag)"))
class DESIGNPATTERNSRPG_API URPG_Objective_KillTag : public URPG_Objective
{
	GENERATED_BODY()

public:
	virtual void BeginTracking_Implementation(URPG_ObjectiveTrackerComponent* Tracker, FGameplayTag QuestTag, FGameplayTag ObjectiveTag) override;
	virtual void EndTracking_Implementation() override;
	virtual FText DescribeObjective() const override;

private:
	/** Bus listener handle, released on EndTracking. */
	FDP_ListenerHandle BusListener;
};

/**
 * Collect objective: tracks how many of TargetTag the owner holds via the read-only ISeam_ItemQuery seam.
 *
 * Unlike kill/reach this is STATE-DERIVED rather than event-counted: on each inventory-change notification
 * it re-reads the seam and reports the ABSOLUTE count (the tracker SETS, not increments, this objective's
 * counter) so dropping items correctly regresses progress. QueryCurrentProgress returns the live count so
 * the tracker seeds/repairs the counter on (re)activation and restore.
 */
UCLASS(meta = (DisplayName = "Collect N (Item)"))
class DESIGNPATTERNSRPG_API URPG_Objective_CollectItem : public URPG_Objective
{
	GENERATED_BODY()

public:
	virtual void BeginTracking_Implementation(URPG_ObjectiveTrackerComponent* Tracker, FGameplayTag QuestTag, FGameplayTag ObjectiveTag) override;
	virtual void EndTracking_Implementation() override;
	virtual int32 QueryCurrentProgress(const URPG_ObjectiveTrackerComponent* Tracker) const override;
	virtual FText DescribeObjective() const override;

private:
	/** Re-read the inventory seam and push the absolute count to the tracker. */
	void Resync();

	/** Resolve the item-query seam implementer (component or actor) off the tracker's owning actor; null if none. */
	UObject* ResolveItemQuery() const;

	/** Bus listener handle for the project inventory-changed channel, released on EndTracking. */
	FDP_ListenerHandle BusListener;
};

/**
 * Reach-location objective: completes when a "reached" bus event for TargetTag arrives.
 *
 * Listens on BusChannel (default DP.Bus.RPG.Objective.Reach). A trigger volume / nav system broadcasts an
 * FRPG_QuestBusEvent with the reached location tag in NodeTag; this evaluator reports full completion when
 * NodeTag matches TargetTag. Reaching a place is binary, so it reports the RequiredCount delta and the
 * tracker clamps.
 */
UCLASS(meta = (DisplayName = "Reach Location"))
class DESIGNPATTERNSRPG_API URPG_Objective_ReachLocation : public URPG_Objective
{
	GENERATED_BODY()

public:
	virtual void BeginTracking_Implementation(URPG_ObjectiveTrackerComponent* Tracker, FGameplayTag QuestTag, FGameplayTag ObjectiveTag) override;
	virtual void EndTracking_Implementation() override;
	virtual FText DescribeObjective() const override;

private:
	FDP_ListenerHandle BusListener;
};

/**
 * Talk-to-NPC objective: completes when a dialogue with TargetTag finishes.
 *
 * Listens on BusChannel (default DP.Bus.Narrative.DialogueFinished — bus coupling to Narrative, NOT a
 * header include). Because RPG cannot see the concrete FNarr_DialogueBusEvent type, the project is expected
 * to rebroadcast dialogue completion as an FRPG_QuestBusEvent (the conventional shape) on this channel with
 * the spoken-to NPC in NodeTag; this evaluator reports full completion when NodeTag matches TargetTag.
 */
UCLASS(meta = (DisplayName = "Talk To NPC"))
class DESIGNPATTERNSRPG_API URPG_Objective_TalkToNpc : public URPG_Objective
{
	GENERATED_BODY()

public:
	virtual void BeginTracking_Implementation(URPG_ObjectiveTrackerComponent* Tracker, FGameplayTag QuestTag, FGameplayTag ObjectiveTag) override;
	virtual void EndTracking_Implementation() override;
	virtual FText DescribeObjective() const override;

private:
	FDP_ListenerHandle BusListener;
};

/**
 * Escort objective: completes when the escort target reaches safety, fails when it is lost.
 *
 * Listens on BusChannel (default DP.Bus.RPG.Objective.Escort). The escort system broadcasts an
 * FRPG_QuestBusEvent with the target tag in NodeTag and a status code in IntValue (>0 = delivered safely,
 * <0 = lost). A delivered event matching TargetTag reports full completion; a lost event asks the tracker
 * to fail the quest.
 */
UCLASS(meta = (DisplayName = "Escort"))
class DESIGNPATTERNSRPG_API URPG_Objective_Escort : public URPG_Objective
{
	GENERATED_BODY()

public:
	virtual void BeginTracking_Implementation(URPG_ObjectiveTrackerComponent* Tracker, FGameplayTag QuestTag, FGameplayTag ObjectiveTag) override;
	virtual void EndTracking_Implementation() override;
	virtual FText DescribeObjective() const override;

private:
	FDP_ListenerHandle BusListener;
};

/**
 * Defend objective: completes when a defend timer/wave completes, fails when the defended target falls.
 *
 * Listens on BusChannel (default DP.Bus.RPG.Objective.Defend). The defend system broadcasts an
 * FRPG_QuestBusEvent with the defended target tag in NodeTag and a status code in IntValue (>0 = survived /
 * wave cleared, <0 = target destroyed). Survived reports full completion; destroyed fails the quest.
 */
UCLASS(meta = (DisplayName = "Defend"))
class DESIGNPATTERNSRPG_API URPG_Objective_Defend : public URPG_Objective
{
	GENERATED_BODY()

public:
	virtual void BeginTracking_Implementation(URPG_ObjectiveTrackerComponent* Tracker, FGameplayTag QuestTag, FGameplayTag ObjectiveTag) override;
	virtual void EndTracking_Implementation() override;
	virtual FText DescribeObjective() const override;

private:
	FDP_ListenerHandle BusListener;
};
