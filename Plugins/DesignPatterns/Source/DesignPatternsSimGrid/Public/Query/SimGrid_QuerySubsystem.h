// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "UObject/ScriptInterface.h"
#include "Grid/Seam_GridCoord.h"
#include "Grid/Seam_TileProviderRead.h"
#include "SimGrid_QuerySubsystem.generated.h"

/** Which neighbours of a cell a neighbourhood query considers. */
UENUM(BlueprintType)
enum class ESimGrid_Adjacency : uint8
{
	/** The four orthogonal neighbours (von Neumann). */
	Orthogonal,
	/** The eight surrounding neighbours including diagonals (Moore). */
	EightWay
};

/** Shape used by region queries to bound the cells they collect. */
UENUM(BlueprintType)
enum class ESimGrid_RegionShape : uint8
{
	/** Axis-aligned square of side (2*radius + 1) centred on the origin. */
	Square,
	/** Filled Euclidean disc of the given radius (in cells). */
	Disc,
	/** Diamond / Manhattan-radius region (|dx| + |dy| <= radius). */
	Diamond
};

/**
 * Stateless world subsystem for bounded spatial queries over the SimGrid, expressed entirely against
 * the read seam (ISeam_TileProviderRead) so the queries work over ANY grid implementation. It holds no
 * state of its own — every method takes the grid to query — and EVERY query is hard-bounded by the caps
 * in USimGrid_DeveloperSettings (radius / region-cell / flood-fill / line caps), so no single call can
 * spin the game thread regardless of the requested size. All queries are BlueprintPure and client-safe;
 * results over Unknown cells are reported faithfully (a cell the client hasn't received is simply not a
 * match), never fabricated.
 */
UCLASS()
class DESIGNPATTERNSSIMGRID_API USimGrid_QuerySubsystem : public UDP_WorldSubsystem
{
	GENERATED_BODY()

public:
	/**
	 * UWorldSubsystem has no HasWorldAuthority helper; declare our own. These queries are read-only and
	 * client-safe, so this is provided only for completeness / debug strings.
	 */
	bool HasWorldAuthority() const
	{
		const UWorld* W = GetWorld();
		return W && W->GetNetMode() != NM_Client;
	}

	/**
	 * The valid neighbours of Cell under the given adjacency. Bounded by construction (<= 8). Only cells
	 * the grid reports valid (IsValidCell) are returned, so callers needn't bounds-check.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimGrid|Query")
	TArray<FSeam_CellCoord> GetNeighbors(const TScriptInterface<ISeam_TileProviderRead>& Grid,
		const FSeam_CellCoord& Cell, ESimGrid_Adjacency Adjacency) const;

	/**
	 * Every valid cell within RadiusCells of Center under the given Shape. RadiusCells is clamped to
	 * MaxQueryRadiusCells and the collected count to MaxRegionCells (the query stops collecting at the
	 * cap, nearest-first by ring). Client-safe.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimGrid|Query")
	TArray<FSeam_CellCoord> GetRegion(const TScriptInterface<ISeam_TileProviderRead>& Grid,
		const FSeam_CellCoord& Center, int32 RadiusCells, ESimGrid_RegionShape Shape) const;

	/**
	 * 4-connected flood fill from Start over cells whose snapshot tile-type matches MatchTileType
	 * (hierarchy-matched; an invalid MatchTileType matches EMPTY known cells). Visits at most
	 * MaxFloodFillCells; if the region is larger the fill stops and bOutTruncated is set. Unknown cells
	 * are treated as walls (not matched and not crossed), so a client fill never leaks across
	 * unreplicated regions. Client-safe.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimGrid|Query",
		meta = (AutoCreateRefTerm = "MatchTileType"))
	TArray<FSeam_CellCoord> FloodFill(const TScriptInterface<ISeam_TileProviderRead>& Grid,
		const FSeam_CellCoord& Start, FGameplayTag MatchTileType, bool& bOutTruncated) const;

	/**
	 * The cells a straight line from Start to End passes through (Bresenham, inclusive of both ends),
	 * clipped to valid cells and to MaxLineCells. Useful for line-of-sight / drag-painting. Client-safe.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimGrid|Query")
	TArray<FSeam_CellCoord> GetLine(const TScriptInterface<ISeam_TileProviderRead>& Grid,
		const FSeam_CellCoord& Start, const FSeam_CellCoord& End) const;

	//~ Begin UDP_WorldSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_WorldSubsystem

private:
	/** Resolve the effective query caps from USimGrid_DeveloperSettings (never null in a project). */
	static void GetCaps(int32& OutMaxRadius, int32& OutMaxRegion, int32& OutMaxFlood, int32& OutMaxLine);

	/** True if a snapshot's tile-type satisfies MatchTileType (invalid MatchTileType == "known empty"). */
	static bool SnapshotMatches(const FSeam_CellSnapshot& Snapshot, const FGameplayTag& MatchTileType);
};
