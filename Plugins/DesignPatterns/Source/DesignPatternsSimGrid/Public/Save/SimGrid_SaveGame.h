// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Save/DPSaveGame.h"
#include "GameplayTagContainer.h"
#include "Grid/Seam_GridCoord.h"
#include "Identity/Seam_EntityId.h"
#include "Zone/SimGrid_ZoneTypes.h"

// FInstancedStruct version-gated include. A full FInstancedStruct is fine in a SAVE object (it is
// serialized via UPROPERTY SaveGame, not replicated) — the no-plain-replicated rule is net-only.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "SimGrid_SaveGame.generated.h"

class USimGrid_WorldSubsystem;

/**
 * One persisted cell: coordinate, tile type and full payload. Save-side records may carry a complete
 * FInstancedStruct payload because saves are local-only (the net path uses the fast array instead).
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMGRID_API FSimGrid_SavedCell
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "SimGrid|Save")
	FSeam_CellCoord Coord;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "SimGrid|Save")
	FGameplayTag TileTypeTag;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "SimGrid|Save")
	FInstancedStruct Payload;

	FSimGrid_SavedCell() = default;
	FSimGrid_SavedCell(const FSeam_CellCoord& InCoord, const FGameplayTag& InTag, const FInstancedStruct& InPayload)
		: Coord(InCoord), TileTypeTag(InTag), Payload(InPayload) {}
};

/** One persisted ownership claim: which cell, owned by which stable entity id. */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMGRID_API FSimGrid_SavedOwnership
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "SimGrid|Save")
	FSeam_CellCoord Coord;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "SimGrid|Save")
	FSeam_EntityId OwnerId;

	FSimGrid_SavedOwnership() = default;
	FSimGrid_SavedOwnership(const FSeam_CellCoord& InCoord, const FSeam_EntityId& InOwner)
		: Coord(InCoord), OwnerId(InOwner) {}
};

/**
 * Save object for the SimGrid world: a flat list of placed cells and ownership claims, plus the grid
 * layout (cell/chunk size) it was captured with.
 *
 * CaptureFrom walks the world subsystem's chunk carriers on AUTHORITY and snapshots every cell/claim.
 * RestoreInto rebuilds the grid by routing each record back through the world subsystem's authority
 * mutators (which delegate to carrier ApplyCell/ClaimCells), so restore obeys the same authority and
 * replication path as live placement — never writing replicated state directly.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSSIMGRID_API USimGrid_SaveGame : public UDP_SaveGame
{
	GENERATED_BODY()

public:
	/** Cell size (world units) the grid was saved with, for validation/migration on restore. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "SimGrid|Save")
	float CellSize = 100.f;

	/** Chunk size (cells per side) the grid was saved with. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "SimGrid|Save")
	FIntPoint ChunkSize = FIntPoint(16, 16);

	/** Every placed cell at capture time. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "SimGrid|Save")
	TArray<FSimGrid_SavedCell> Cells;

	/** Every ownership claim at capture time. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "SimGrid|Save")
	TArray<FSimGrid_SavedOwnership> Ownership;

	/**
	 * Every painted zone cell at capture time. Captured from the authority zone carrier. Additive
	 * field: saves written before zone support load with an empty Zones array and restore silently
	 * (RestoreInto ignores it when no carrier is present), so old saves remain compatible.
	 */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "SimGrid|Save")
	TArray<FSimGrid_SavedZone> Zones;

	/**
	 * Snapshot the grid from the world subsystem's carriers. AUTHORITY ONLY (returns false and logs on
	 * a client, since clients lack the full authoritative state). Replaces this object's record arrays.
	 * @return True if capture ran on authority.
	 */
	UFUNCTION(BlueprintCallable, Category = "SimGrid|Save")
	bool CaptureFrom(USimGrid_WorldSubsystem* Grid);

	/**
	 * Restore the saved grid into the world subsystem by re-applying every cell and claim through its
	 * authority mutators. AUTHORITY ONLY (returns 0 on a client). Existing cells in restored coords are
	 * overwritten; cells absent from the save are left untouched (call ClearAll-style logic upstream if
	 * a full reset is desired).
	 * @return Number of cell records applied.
	 */
	UFUNCTION(BlueprintCallable, Category = "SimGrid|Save")
	int32 RestoreInto(USimGrid_WorldSubsystem* Grid) const;

	//~ Begin UDP_SaveGame
	virtual void OnPreSave_Implementation() override;
	virtual void OnPostLoad_Implementation() override;
	//~ End UDP_SaveGame
};
