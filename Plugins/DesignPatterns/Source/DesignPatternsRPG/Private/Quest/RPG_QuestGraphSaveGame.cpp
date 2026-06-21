// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Quest/RPG_QuestGraphSaveGame.h"
#include "Quest/RPG_ObjectiveTrackerComponent.h"
#include "Core/DPLog.h"

void URPG_QuestGraphSaveGame::CaptureStagesFrom(URPG_ObjectiveTrackerComponent* TrackerComp)
{
	if (!TrackerComp)
	{
		return;
	}
	SavedStages = TrackerComp->ExportStageStates();
	UE_LOG(LogDP, Log, TEXT("[RPG] Captured %d quest stage states."), SavedStages.Num());
}

void URPG_QuestGraphSaveGame::RestoreStagesInto(URPG_ObjectiveTrackerComponent* TrackerComp) const
{
	if (!TrackerComp)
	{
		return;
	}
	// Authority guard lives inside ImportStageStates (a client-side load is a no-op there).
	TrackerComp->ImportStageStates(SavedStages);
}
