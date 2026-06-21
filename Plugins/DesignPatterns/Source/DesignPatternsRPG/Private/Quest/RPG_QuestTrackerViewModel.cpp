// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Quest/RPG_QuestTrackerViewModel.h"
#include "Quest/RPG_ObjectiveTrackerComponent.h"
#include "Quest/RPG_QuestGraphDefinition.h"
#include "Quest/RPG_QuestGraphTypes.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Data/DPDataRegistrySubsystem.h"

void URPG_QuestTrackerViewModel::Bind(URPG_QuestLogComponent* InLog, URPG_ObjectiveTrackerComponent* InTracker)
{
	Unbind();

	Log = InLog;
	Tracker = InTracker;

	if (URPG_QuestLogComponent* L = Log.Get())
	{
		L->OnQuestStateChanged.AddDynamic(this, &URPG_QuestTrackerViewModel::HandleQuestStateChanged);
		L->OnObjectiveProgress.AddDynamic(this, &URPG_QuestTrackerViewModel::HandleObjectiveProgress);
	}
	if (URPG_ObjectiveTrackerComponent* T = Tracker.Get())
	{
		T->OnStageAdvanced.AddDynamic(this, &URPG_QuestTrackerViewModel::HandleStageAdvanced);
	}

	OnTrackerRefreshed.Broadcast();
}

void URPG_QuestTrackerViewModel::Unbind()
{
	if (URPG_QuestLogComponent* L = Log.Get())
	{
		L->OnQuestStateChanged.RemoveDynamic(this, &URPG_QuestTrackerViewModel::HandleQuestStateChanged);
		L->OnObjectiveProgress.RemoveDynamic(this, &URPG_QuestTrackerViewModel::HandleObjectiveProgress);
	}
	if (URPG_ObjectiveTrackerComponent* T = Tracker.Get())
	{
		T->OnStageAdvanced.RemoveDynamic(this, &URPG_QuestTrackerViewModel::HandleStageAdvanced);
	}
	Log.Reset();
	Tracker.Reset();
}

void URPG_QuestTrackerViewModel::SetTrackedQuest(FGameplayTag QuestTag)
{
	ExplicitQuest = QuestTag;
	OnTrackerRefreshed.Broadcast();
}

FGameplayTag URPG_QuestTrackerViewModel::GetTrackedQuest() const
{
	if (ExplicitQuest.IsValid())
	{
		return ExplicitQuest;
	}
	if (const URPG_QuestLogComponent* L = Log.Get())
	{
		const FGameplayTagContainer Active = L->GetActiveQuests();
		for (const FGameplayTag& Tag : Active)
		{
			return Tag; // first active quest
		}
	}
	return FGameplayTag();
}

FGameplayTag URPG_QuestTrackerViewModel::GetTrackedStage() const
{
	if (const URPG_ObjectiveTrackerComponent* T = Tracker.Get())
	{
		return T->GetCurrentStage(GetTrackedQuest());
	}
	return FGameplayTag();
}

URPG_QuestGraphDefinition* URPG_QuestTrackerViewModel::ResolveGraph(const FGameplayTag& QuestTag) const
{
	if (!QuestTag.IsValid())
	{
		return nullptr;
	}
	if (UDP_DataRegistrySubsystem* Registry =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_DataRegistrySubsystem>(this))
	{
		return Cast<URPG_QuestGraphDefinition>(Registry->FindByTag(QuestTag));
	}
	return nullptr;
}

FText URPG_QuestTrackerViewModel::GetTrackedStageTitle() const
{
	const FGameplayTag QuestTag = GetTrackedQuest();
	const FGameplayTag StageTag = GetTrackedStage();
	if (const URPG_QuestGraphDefinition* Graph = ResolveGraph(QuestTag))
	{
		if (const FRPG_QuestStage* Stage = Graph->FindStage(StageTag))
		{
			return Stage->StageTitle;
		}
	}
	return FText::GetEmpty();
}

TArray<FRPG_TrackedObjectiveView> URPG_QuestTrackerViewModel::GetTrackedObjectives() const
{
	TArray<FRPG_TrackedObjectiveView> Views;

	const FGameplayTag QuestTag = GetTrackedQuest();
	const URPG_QuestLogComponent* L = Log.Get();
	const URPG_ObjectiveTrackerComponent* T = Tracker.Get();
	if (!QuestTag.IsValid() || !L)
	{
		return Views;
	}

	const URPG_QuestGraphDefinition* Graph = ResolveGraph(QuestTag);
	if (Graph && Graph->IsBranching() && T)
	{
		// Branching: show only the current stage's visible objectives.
		const FGameplayTag StageTag = T->GetCurrentStage(QuestTag);
		const FRPG_QuestStage* Stage = Graph->FindStage(StageTag);
		if (Stage)
		{
			for (const FRPG_QuestStageObjective& Slot : Stage->Objectives)
			{
				if (Slot.bHidden && T->IsObjectiveHidden(QuestTag, Slot.Objective.ObjectiveTag))
				{
					continue; // hidden, not yet revealed
				}
				FRPG_TrackedObjectiveView View;
				View.ObjectiveTag = Slot.Objective.ObjectiveTag;
				View.Description = Slot.Objective.Description;
				View.Required = FMath::Max(1, Slot.Objective.RequiredCount);
				View.Current = L->GetObjectiveCount(QuestTag, Slot.Objective.ObjectiveTag);
				View.bOptional = Slot.bOptional;
				View.bComplete = View.Current >= View.Required;
				Views.Add(View);
			}
		}
		return Views;
	}

	// Linear fallback: show the definition's flat objectives. Resolve the BASE quest definition (the quest
	// may be a plain URPG_QuestDefinition, not the branching subclass).
	const URPG_QuestDefinition* Def = Graph;
	if (!Def)
	{
		if (UDP_DataRegistrySubsystem* Registry =
			FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_DataRegistrySubsystem>(this))
		{
			Def = Cast<URPG_QuestDefinition>(Registry->FindByTag(QuestTag));
		}
	}
	if (Def)
	{
		for (const FRPG_QuestObjective& Obj : Def->Objectives)
		{
			FRPG_TrackedObjectiveView View;
			View.ObjectiveTag = Obj.ObjectiveTag;
			View.Description = Obj.Description;
			View.Required = FMath::Max(1, Obj.RequiredCount);
			View.Current = L->GetObjectiveCount(QuestTag, Obj.ObjectiveTag);
			View.bComplete = View.Current >= View.Required;
			Views.Add(View);
		}
	}
	return Views;
}

float URPG_QuestTrackerViewModel::GetStageTimeRemaining() const
{
	if (const URPG_ObjectiveTrackerComponent* T = Tracker.Get())
	{
		return T->GetStageTimeRemaining(GetTrackedQuest());
	}
	return -1.f;
}

void URPG_QuestTrackerViewModel::HandleQuestStateChanged(URPG_QuestLogComponent* /*QuestLog*/, FGameplayTag /*QuestTag*/, ERPG_QuestState /*NewState*/)
{
	OnTrackerRefreshed.Broadcast();
}

void URPG_QuestTrackerViewModel::HandleObjectiveProgress(URPG_QuestLogComponent* /*QuestLog*/, FGameplayTag /*QuestTag*/, FGameplayTag /*ObjectiveTag*/, int32 /*NewCount*/)
{
	OnTrackerRefreshed.Broadcast();
}

void URPG_QuestTrackerViewModel::HandleStageAdvanced(FGameplayTag /*QuestTag*/, FGameplayTag /*NewStage*/)
{
	OnTrackerRefreshed.Broadcast();
}
