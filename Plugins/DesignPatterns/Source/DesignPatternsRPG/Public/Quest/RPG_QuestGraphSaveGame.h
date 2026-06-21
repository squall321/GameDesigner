// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Quest/RPG_QuestSaveGame.h"
#include "Quest/RPG_ObjectiveTrackerComponent.h"   // FRPG_QuestStageState
#include "GameplayTagContainer.h"
#include "RPG_QuestGraphSaveGame.generated.h"

class URPG_ObjectiveTrackerComponent;

/**
 * Extends the real quest save with the branching layer's stage state + lore unlocks.
 *
 * The base URPG_QuestSaveGame already persists per-objective counters (CaptureFrom/RestoreInto the quest
 * log). This subclass adds the stage cursor (current stage, revealed-hidden objectives, visited stages, and
 * the elapsed-time fields the tracker re-anchors on restore so a stage/quest time limit resumes correctly)
 * and the set of unlocked lore tags. CaptureStagesFrom runs on the server; RestoreStagesInto is authority-
 * guarded by the tracker itself.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSRPG_API URPG_QuestGraphSaveGame : public URPG_QuestSaveGame
{
	GENERATED_BODY()

public:
	/** Persisted per-quest stage state (elapsed-time encoded; the tracker re-anchors on restore). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "RPG|Quest|Graph")
	TArray<FRPG_QuestStageState> SavedStages;

	/** Persisted unlocked lore tags (canonical state also lives as hub flags; this mirrors for offline UI). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "RPG|Quest|Graph")
	FGameplayTagContainer UnlockedLore;

	/** Gather a tracker's stage states into this save (server side). */
	UFUNCTION(BlueprintCallable, Category = "RPG|Quest|Graph")
	void CaptureStagesFrom(URPG_ObjectiveTrackerComponent* TrackerComp);

	/** Restore stage states into a tracker (authority-guarded by the tracker). */
	UFUNCTION(BlueprintCallable, Category = "RPG|Quest|Graph")
	void RestoreStagesInto(URPG_ObjectiveTrackerComponent* TrackerComp) const;
};
