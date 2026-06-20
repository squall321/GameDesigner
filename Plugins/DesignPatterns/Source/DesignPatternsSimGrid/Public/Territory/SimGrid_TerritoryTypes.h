// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "GameplayTagContainer.h"
#include "UObject/Interface.h"
#include "Grid/Seam_GridCoord.h"
#include "SimGrid_TerritoryTypes.generated.h"

class USimGrid_TerritoryComponent;

UINTERFACE(BlueprintType, MinimalAPI)
class USimGrid_OwnershipRead : public UInterface
{
	GENERATED_BODY()
};

/**
 * Small read-only ownership seam. The territory carrier implements it so rules (USimGrid_Rule_OwnedZone)
 * and other systems can ask "who owns this cell?" WITHOUT hard-including the territory component. The
 * placement rule resolves this interface off the grid-provider actor, keeping cross-type coupling at a
 * seam. All methods are const and client-safe (clients answer from replicated ownership state, which
 * may be incomplete — callers treat an invalid owner as "unknown/neutral").
 */
class DESIGNPATTERNSSIMGRID_API ISimGrid_OwnershipRead
{
	GENERATED_BODY()

public:
	/** Owning identity tag of Cell, or an invalid tag if the cell is unowned / not yet replicated. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "SimGrid|Territory")
	FGameplayTag GetCellOwner(const FSeam_CellCoord& Cell) const;

	/** True if Cell is owned by OwnerId. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "SimGrid|Territory")
	bool IsOwnedBy(const FSeam_CellCoord& Cell, const FGameplayTag& OwnerId) const;

	/** True if the local machine knows this cell's ownership state at all (false on a client lacking it). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "SimGrid|Territory")
	bool IsOwnershipKnown(const FSeam_CellCoord& Cell) const;
};

/**
 * One replicated cell-ownership record. Wraps a (cell, ownerId) pair as a fast-array item so claims and
 * releases delta-replicate individually rather than resending the whole ownership map. The cell is the
 * map key; releasing a cell removes its entry entirely (absence == unowned).
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMGRID_API FSimGrid_CellOwnershipEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	/** The owned cell. */
	UPROPERTY(BlueprintReadOnly, Category = "SimGrid|Territory")
	FSeam_CellCoord Cell;

	/** Owning identity tag (always valid while the entry exists). */
	UPROPERTY(BlueprintReadOnly, Category = "SimGrid|Territory")
	FGameplayTag OwnerId;

	FSimGrid_CellOwnershipEntry() = default;
	FSimGrid_CellOwnershipEntry(const FSeam_CellCoord& InCell, const FGameplayTag& InOwner)
		: Cell(InCell), OwnerId(InOwner) {}

	//~ FFastArraySerializerItem replication callbacks (clients only).
	void PreReplicatedRemove(const struct FSimGrid_OwnershipArray& InArraySerializer);
	void PostReplicatedAdd(const struct FSimGrid_OwnershipArray& InArraySerializer);
	void PostReplicatedChange(const struct FSimGrid_OwnershipArray& InArraySerializer);
};

/**
 * Fast-array serializer holding the territory's per-cell ownership entries. NetDeltaSerialize forwards
 * to FastArrayDeltaSerialize so only changed cells cross the wire. The non-replicated back-pointer lets
 * the entry callbacks notify the owning component (server and client).
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMGRID_API FSimGrid_OwnershipArray : public FFastArraySerializer
{
	GENERATED_BODY()

	/** Replicated ownership entries (one per owned cell). */
	UPROPERTY(BlueprintReadOnly, Category = "SimGrid|Territory")
	TArray<FSimGrid_CellOwnershipEntry> Entries;

	/** Non-replicated back-pointer to the owning component, for change notifications. */
	UPROPERTY(NotReplicated, Transient)
	TObjectPtr<USimGrid_TerritoryComponent> OwnerComponent = nullptr;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FastArrayDeltaSerialize<FSimGrid_CellOwnershipEntry, FSimGrid_OwnershipArray>(Entries, DeltaParms, *this);
	}
};

/** Enables NetDeltaSerialize for the ownership array. */
template<>
struct TStructOpsTypeTraits<FSimGrid_OwnershipArray> : public TStructOpsTypeTraitsBase2<FSimGrid_OwnershipArray>
{
	enum { WithNetDeltaSerializer = true };
};
