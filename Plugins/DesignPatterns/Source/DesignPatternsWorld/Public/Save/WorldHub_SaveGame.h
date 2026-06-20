// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Save/DPSaveGame.h"
#include "Save/WorldHub_Snapshot.h"
#include "WorldHub_SaveGame.generated.h"

/**
 * The DesignPatterns save object that carries persistent world-hub state.
 *
 * A thin UDP_SaveGame subclass whose body is a single FWorldHub_Snapshot (every field SaveGame-
 * tagged, so the standard serializer captures the FInstancedStruct values losslessly). The
 * game-instance persistent hub builds one of these from its persistent map and hands it to the core
 * save subsystem; on load it applies the snapshot back.
 *
 * OnPreSave/OnPostLoad are intentionally minimal here — the persistent hub owns the gather/scatter
 * because it, not the save object, knows the live persistent map. Migrate() up-converts older
 * snapshot schema versions in place.
 */
UCLASS(BlueprintType, Blueprintable)
class DESIGNPATTERNSWORLD_API UWorldHub_SaveGame : public UDP_SaveGame
{
	GENERATED_BODY()

public:
	/** Bump this whenever the on-disk save schema for the world hub changes (drives Migrate). */
	static constexpr int32 CurrentSaveSchemaVersion = 1;

	/** The persisted world-hub snapshot (the save body). */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "DesignPatterns|WorldHub|Save")
	FWorldHub_Snapshot Snapshot;

	/** The schema version this object was written with, for migration branching. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "DesignPatterns|WorldHub|Save")
	int32 SaveSchemaVersion = CurrentSaveSchemaVersion;

	//~ Begin UDP_SaveGame
	/** No-op by default: the persistent hub fills Snapshot before handing this to the save subsystem. */
	virtual void OnPreSave_Implementation() override;

	/** No-op by default: the persistent hub reads Snapshot back after load. */
	virtual void OnPostLoad_Implementation() override;

	/** Up-convert an older snapshot schema in place. Returns true on success. */
	virtual bool Migrate_Implementation(int32 FromVersion, int32 ToVersion) override;
	//~ End UDP_SaveGame
};
