// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Save/DPSaveGame.h"
#include "Quest/RPG_QuestLogComponent.h"
#include "RPG_QuestSaveGame.generated.h"

/**
 * Concrete core-Save payload for RPG quest progress.
 *
 * Demonstrates the intended persistence path: a quest log's full progress array is gathered
 * into this UDP_SaveGame subclass (CaptureFrom) and written via UDP_SaveGameSubsystem; on
 * load it is restored back into a quest log (RestoreInto). All quest mutation in RestoreInto
 * runs through the authority-guarded ImportProgress, so a client-side load is a no-op on the
 * authoritative state.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSRPG_API URPG_QuestSaveGame : public UDP_SaveGame
{
	GENERATED_BODY()

public:
	/** Persisted quest progress. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Quest")
	TArray<FRPG_QuestProgress> SavedQuests;

	/** Gather a quest log's progress into this save object (server side). */
	UFUNCTION(BlueprintCallable, Category = "RPG|Quest")
	void CaptureFrom(URPG_QuestLogComponent* QuestLog);

	/** Restore this save's progress into a quest log (authority-guarded by ImportProgress). */
	UFUNCTION(BlueprintCallable, Category = "RPG|Quest")
	void RestoreInto(URPG_QuestLogComponent* QuestLog) const;
};
