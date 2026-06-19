// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Quest/RPG_QuestSaveGame.h"

void URPG_QuestSaveGame::CaptureFrom(URPG_QuestLogComponent* QuestLog)
{
	if (QuestLog)
	{
		SavedQuests = QuestLog->ExportProgress();
	}
}

void URPG_QuestSaveGame::RestoreInto(URPG_QuestLogComponent* QuestLog) const
{
	if (QuestLog)
	{
		// ImportProgress is authority-guarded; a client-side call is a safe no-op.
		QuestLog->ImportProgress(SavedQuests);
	}
}
