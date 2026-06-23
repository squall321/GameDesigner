// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "GameplayTagContainer.h"
#include "Grid/Seam_GridCoord.h"
#include "Identity/Seam_EntityId.h"
#include "SimGrid_ZoneTypes.generated.h"

class ASimGrid_ZoneCarrier;

/**
 * One replicated ZONE/district assignment over a cell: which cell, which zone-type tag is painted there,
 * and the owning entity that painted it. Wrapped as a fast-array item so paints/erases delta-replicate
 * per cell instead of resending the whole district map.
 *
 * Zones are a SEPARATE concept from tiles and from territory ownership: a zone is a designer-painted
 * region with rules/effects (residential / commercial / no-build / farm). A cell can simultaneously hold
 * a tile, a territory owner, and a zone — these are independent fast arrays on independent carriers so
 * painting a zone never churns cell or ownership deltas.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMGRID_API FSimGrid_ZoneEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	/** The zoned cell. Unique within the carrier's array. */
	UPROPERTY(BlueprintReadOnly, Category = "SimGrid|Zone")
	FSeam_CellCoord Cell;

	/** The zone-type tag painted on this cell (e.g. SimGrid.Zone.Residential). */
	UPROPERTY(BlueprintReadOnly, Category = "SimGrid|Zone")
	FGameplayTag ZoneTypeTag;

	/** The entity that painted/owns this zone cell (faction/player). Invalid for an unowned paint. */
	UPROPERTY(BlueprintReadOnly, Category = "SimGrid|Zone")
	FSeam_EntityId OwnerId;

	/**
	 * Normalized growth/development level in [0,1] this zone cell has accumulated over sim time. Drives
	 * progressive effects (a residential cell densifying); advanced only on authority by the growth
	 * component. Replicated so clients can render the development stage.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "SimGrid|Zone")
	float GrowthLevel = 0.f;

	FSimGrid_ZoneEntry() = default;
	FSimGrid_ZoneEntry(const FSeam_CellCoord& InCell, const FGameplayTag& InZone, const FSeam_EntityId& InOwner)
		: Cell(InCell), ZoneTypeTag(InZone), OwnerId(InOwner) {}

	//~ FFastArraySerializerItem replication callbacks (invoked on clients only).
	void PreReplicatedRemove(const struct FSimGrid_ZoneArray& InArraySerializer);
	void PostReplicatedAdd(const struct FSimGrid_ZoneArray& InArraySerializer);
	void PostReplicatedChange(const struct FSimGrid_ZoneArray& InArraySerializer);
};

/**
 * Fast-array serializer holding a carrier's painted zone cells. NetDeltaSerialize forwards to
 * FastArrayDeltaSerialize so only changed cells cross the wire. The non-replicated back-pointer lets the
 * item callbacks notify the owning carrier (server and client). This is a DISTINCT type from the
 * cell/ownership fast arrays (no name collision), so this header is safe to include anywhere.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMGRID_API FSimGrid_ZoneArray : public FFastArraySerializer
{
	GENERATED_BODY()

	/** Replicated zone entries for this carrier. */
	UPROPERTY(BlueprintReadOnly, Category = "SimGrid|Zone")
	TArray<FSimGrid_ZoneEntry> Entries;

	/** Non-replicated back-pointer to the owning zone carrier, for change notifications. */
	UPROPERTY(NotReplicated, Transient)
	TObjectPtr<ASimGrid_ZoneCarrier> OwnerCarrier = nullptr;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FastArrayDeltaSerialize<FSimGrid_ZoneEntry, FSimGrid_ZoneArray>(Entries, DeltaParms, *this);
	}
};

/** Enables NetDeltaSerialize for the zone array. */
template<>
struct TStructOpsTypeTraits<FSimGrid_ZoneArray> : public TStructOpsTypeTraitsBase2<FSimGrid_ZoneArray>
{
	enum { WithNetDeltaSerializer = true };
};

/** One persisted zone cell, for the additive SimGrid save extension. */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMGRID_API FSimGrid_SavedZone
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "SimGrid|Save")
	FSeam_CellCoord Cell;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "SimGrid|Save")
	FGameplayTag ZoneTypeTag;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "SimGrid|Save")
	FSeam_EntityId OwnerId;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "SimGrid|Save")
	float GrowthLevel = 0.f;

	FSimGrid_SavedZone() = default;
	FSimGrid_SavedZone(const FSeam_CellCoord& InCell, const FGameplayTag& InZone,
		const FSeam_EntityId& InOwner, float InGrowth)
		: Cell(InCell), ZoneTypeTag(InZone), OwnerId(InOwner), GrowthLevel(InGrowth) {}
};
