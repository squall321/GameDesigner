// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Identity/Seam_EntityId.h"
#include "Relationship/Ent_EntityLinkArray.h"

// FInstancedStruct lives in StructUtils on UE 5.3/5.4, merged into CoreUObject in 5.5.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "Ent_EntitySnapshot.generated.h"

/**
 * The non-core extras captured alongside the entity's own ISeam_Persistable record.
 *
 * The entity component's CaptureState already yields an FInstancedStruct wrapping FEnt_EntitySaveData
 * (identity + per-trait fragments). This struct holds the additional state the snapshot/rewind service
 * tracks that is NOT inside that core record: outgoing relationship links, the replicated tag set, the
 * world transform and the sim time at capture.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSENTITY_API FEnt_EntitySnapshotExtra
{
	GENERATED_BODY()

	/** Outgoing relationship links, flattened to plain records (never fast-array items in a save struct). */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Entity|Snapshot")
	TArray<FEnt_LinkRecord> Links;

	/** The entity's replicated tag set at capture (explicit tags only). */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Entity|Snapshot")
	FGameplayTagContainer ReplicatedTags;

	/** The owning actor's world transform at capture. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Entity|Snapshot")
	FTransform Transform = FTransform::Identity;

	FEnt_EntitySnapshotExtra() = default;
};

/**
 * A complete capture of one entity. LOCAL/SAVE only — NEVER replicated.
 *
 * CoreSave is a plain FInstancedStruct wrapping FEnt_EntitySaveData (the same pattern as
 * FEnt_TraitSaveFragment::TraitState). Extra holds links/tags/transform. SimTimeSeconds stamps the
 * capture for the rewind ring buffer.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSENTITY_API FEnt_EntitySnapshot
{
	GENERATED_BODY()

	/** The entity this snapshot describes. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Entity|Snapshot")
	FSeam_EntityId EntityId;

	/** The entity's own ISeam_Persistable record (FEnt_EntitySaveData), save-only. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Entity|Snapshot")
	FInstancedStruct CoreSave;

	/** Links/tags/transform captured outside the core record. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Entity|Snapshot")
	FEnt_EntitySnapshotExtra Extra;

	/** Deterministic sim time (from ISeam_SimClock if available, else world time) at capture. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Entity|Snapshot")
	double SimTimeSeconds = 0.0;

	FEnt_EntitySnapshot() = default;

	/** True when this snapshot names a real entity. */
	bool IsValidSnapshot() const { return EntityId.IsValid(); }
};

/**
 * A snapshot of many entities (a world capture). LOCAL/SAVE only — never replicated.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSENTITY_API FEnt_WorldSnapshot
{
	GENERATED_BODY()

	/** Per-entity snapshots. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Entity|Snapshot")
	TArray<FEnt_EntitySnapshot> Entities;

	/** Sim time the world capture was taken. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Entity|Snapshot")
	double SimTimeSeconds = 0.0;

	FEnt_WorldSnapshot() = default;
};
