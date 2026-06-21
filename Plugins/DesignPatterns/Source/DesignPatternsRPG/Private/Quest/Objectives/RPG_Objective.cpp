// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Quest/Objectives/RPG_Objective.h"
#include "Quest/RPG_ObjectiveTrackerComponent.h"
#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "MessageBus/DPMessageBusSubsystem.h"

void URPG_Objective::BeginTracking_Implementation(URPG_ObjectiveTrackerComponent* Tracker, FGameplayTag QuestTag, FGameplayTag InObjectiveTag)
{
	OwningTracker = Tracker;
	CachedQuestTag = QuestTag;
	// Subclasses that did not author an explicit ObjectiveTag inherit the slot's tag passed in.
	if (!ObjectiveTag.IsValid())
	{
		ObjectiveTag = InObjectiveTag;
	}
}

void URPG_Objective::EndTracking_Implementation()
{
	// Subclasses unsubscribe their bus listeners before clearing the tracker.
	if (UDP_MessageBusSubsystem* Bus = GetBus())
	{
		Bus->StopListeningForOwner(this);
	}
	OwningTracker.Reset();
}

FText URPG_Objective::DescribeObjective() const
{
	return GetClass() ? FText::FromName(GetClass()->GetFName()) : FText::GetEmpty();
}

void URPG_Objective::ReportToTracker(int32 Delta)
{
	if (URPG_ObjectiveTrackerComponent* Tracker = OwningTracker.Get())
	{
		Tracker->ReportProgress(CachedQuestTag, ObjectiveTag, Delta);
	}
}

UDP_MessageBusSubsystem* URPG_Objective::GetBus() const
{
	const URPG_ObjectiveTrackerComponent* Tracker = OwningTracker.Get();
	if (!Tracker)
	{
		return nullptr;
	}
	return FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(Tracker);
}
