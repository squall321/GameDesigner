// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "GameplayTagContainer.h"
#include "Grid/Seam_GridCoord.h"
#include "Identity/Seam_EntityId.h"

// FInstancedStruct lives in StructUtils on UE 5.3/5.4, merged into CoreUObject in 5.5. A plain
// Replicated FInstancedStruct is forbidden, but inside a FFastArraySerializerItem the engine
// delta-serializes it correctly (it serializes the item via its UPROPERTYs), so it is a normal member.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "SimGrid_CellArray.generated.h"

class ASimGrid_ChunkReplicator;

/**
 * One replicated grid cell: its coordinate, the tile-type tag placed there, and an optional per-cell
 * payload. Wrapped as a fast-array item so individual cell placements/clears/changes delta-replicate
 * instead of resending the whole chunk.
 *
 * The Payload is a NORMAL FInstancedStruct member (NOT a standalone Replicated UPROPERTY): the fast
 * array serializes each item through its UPROPERTYs and the engine handles the instanced struct's
 * type+memory, which is the supported way to replicate arbitrary per-cell state.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMGRID_API FSimGrid_CellEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	/** Cell coordinate this entry describes. Unique within the owning chunk's array. */
	UPROPERTY(BlueprintReadOnly, Category = "SimGrid|Cell")
	FSeam_CellCoord Coord;

	/** Identity tag of the placed tile type (matches USimGrid_TileTypeDefinition::DataTag). */
	UPROPERTY(BlueprintReadOnly, Category = "SimGrid|Cell")
	FGameplayTag TileTypeTag;

	/** Optional per-cell state, seeded from the tile type's DefaultPayloadTemplate or set explicitly. */
	UPROPERTY(BlueprintReadOnly, Category = "SimGrid|Cell")
	FInstancedStruct Payload;

	FSimGrid_CellEntry() = default;
	FSimGrid_CellEntry(const FSeam_CellCoord& InCoord, const FGameplayTag& InTag)
		: Coord(InCoord), TileTypeTag(InTag) {}

	//~ FFastArraySerializerItem replication callbacks (invoked on clients only).
	void PreReplicatedRemove(const struct FSimGrid_CellArray& InArraySerializer);
	void PostReplicatedAdd(const struct FSimGrid_CellArray& InArraySerializer);
	void PostReplicatedChange(const struct FSimGrid_CellArray& InArraySerializer);
};

/**
 * Fast-array serializer holding a chunk's placed cells. NetDeltaSerialize forwards to
 * FastArrayDeltaSerialize so only changed cells cross the wire. The owning carrier back-pointer is
 * non-replicated and set on both server and client so per-item callbacks can notify it.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMGRID_API FSimGrid_CellArray : public FFastArraySerializer
{
	GENERATED_BODY()

	/** Replicated cell entries for this chunk. */
	UPROPERTY(BlueprintReadOnly, Category = "SimGrid|Cell")
	TArray<FSimGrid_CellEntry> Entries;

	/** Non-replicated back-pointer to the owning chunk carrier, for change notifications. */
	UPROPERTY(NotReplicated, Transient)
	TObjectPtr<ASimGrid_ChunkReplicator> OwnerCarrier = nullptr;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FastArrayDeltaSerialize<FSimGrid_CellEntry, FSimGrid_CellArray>(Entries, DeltaParms, *this);
	}
};

/** Enables NetDeltaSerialize for the cell array. */
template<>
struct TStructOpsTypeTraits<FSimGrid_CellArray> : public TStructOpsTypeTraitsBase2<FSimGrid_CellArray>
{
	enum { WithNetDeltaSerializer = true };
};

/**
 * One replicated territory/ownership claim over a cell: which cell, and the owning entity's stable id.
 * Kept as a separate fast array from cells so claiming/releasing territory does not churn cell deltas.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMGRID_API FSimGrid_OwnershipEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	/** Cell whose ownership this entry records. */
	UPROPERTY(BlueprintReadOnly, Category = "SimGrid|Ownership")
	FSeam_CellCoord Coord;

	/** Stable id of the owning entity (faction/player/structure). Invalid means unowned. */
	UPROPERTY(BlueprintReadOnly, Category = "SimGrid|Ownership")
	FSeam_EntityId OwnerId;

	FSimGrid_OwnershipEntry() = default;
	FSimGrid_OwnershipEntry(const FSeam_CellCoord& InCoord, const FSeam_EntityId& InOwner)
		: Coord(InCoord), OwnerId(InOwner) {}

	//~ FFastArraySerializerItem replication callbacks (invoked on clients only).
	void PreReplicatedRemove(const struct FSimGrid_OwnershipArray& InArraySerializer);
	void PostReplicatedAdd(const struct FSimGrid_OwnershipArray& InArraySerializer);
	void PostReplicatedChange(const struct FSimGrid_OwnershipArray& InArraySerializer);
};

/**
 * Fast-array serializer holding a chunk's ownership claims. Delta-replicates per claim. The owning
 * carrier back-pointer is non-replicated and set on server and client for change notifications.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMGRID_API FSimGrid_OwnershipArray : public FFastArraySerializer
{
	GENERATED_BODY()

	/** Replicated ownership entries for this chunk. */
	UPROPERTY(BlueprintReadOnly, Category = "SimGrid|Ownership")
	TArray<FSimGrid_OwnershipEntry> Entries;

	/** Non-replicated back-pointer to the owning chunk carrier, for change notifications. */
	UPROPERTY(NotReplicated, Transient)
	TObjectPtr<ASimGrid_ChunkReplicator> OwnerCarrier = nullptr;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FastArrayDeltaSerialize<FSimGrid_OwnershipEntry, FSimGrid_OwnershipArray>(Entries, DeltaParms, *this);
	}
};

/** Enables NetDeltaSerialize for the ownership array. */
template<>
struct TStructOpsTypeTraits<FSimGrid_OwnershipArray> : public TStructOpsTypeTraitsBase2<FSimGrid_OwnershipArray>
{
	enum { WithNetDeltaSerializer = true };
};
