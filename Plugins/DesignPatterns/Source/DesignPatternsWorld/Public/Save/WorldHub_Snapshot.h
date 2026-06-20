// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Hub/WorldHub_Scope.h"
#include "Registry/WorldHub_FlagTypes.h"
#include "WorldHub_Snapshot.generated.h"

/**
 * One persisted world-hub value: its (Scope, Key) address plus the full value record.
 *
 * The value record (FWorldHub_FlagValue) carries an FInstancedStruct payload tagged SaveGame, so the
 * standard SaveGame serializer captures arbitrary value types losslessly. Scope and Key are flat,
 * net-/save-stable fields (no weak object references), so a snapshot round-trips cleanly through a
 * save file and across level travel.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSWORLD_API FWorldHub_SnapshotEntry
{
	GENERATED_BODY()

	/** The scope this value belongs to. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "DesignPatterns|WorldHub|Save")
	FWorldHub_Scope Scope;

	/** The flag key. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "DesignPatterns|WorldHub|Save")
	FGameplayTag Key;

	/** The captured value record (FInstancedStruct payload + replicate/save policy). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "DesignPatterns|WorldHub|Save")
	FWorldHub_FlagValue Value;

	FWorldHub_SnapshotEntry() = default;
	FWorldHub_SnapshotEntry(const FWorldHub_Scope& InScope, const FGameplayTag& InKey, const FWorldHub_FlagValue& InValue)
		: Scope(InScope), Key(InKey), Value(InValue) {}
};

/**
 * A serializable snapshot of world-hub state.
 *
 * Used in three places: as the FInstancedStruct payload of the hub's ISeam_Persistable capture; as
 * the body the UWorldHub_SaveGame serializes; and as the transfer shape the game-instance
 * persistent hub seeds/receives across level travel. All fields are flat and SaveGame-tagged.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSWORLD_API FWorldHub_Snapshot
{
	GENERATED_BODY()

	/** Schema version of this snapshot's contents, for forward migration. Bump when fields change. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "DesignPatterns|WorldHub|Save")
	int32 SnapshotVersion = 1;

	/** Every captured (Scope, Key, Value) record. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "DesignPatterns|WorldHub|Save")
	TArray<FWorldHub_SnapshotEntry> Entries;

	/** Reset to an empty snapshot at the current version. */
	void Reset() { Entries.Reset(); }

	/** Number of captured entries. */
	int32 Num() const { return Entries.Num(); }
};
