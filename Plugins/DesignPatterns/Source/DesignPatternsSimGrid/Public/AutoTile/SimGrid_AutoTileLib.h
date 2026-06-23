// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "UObject/ScriptInterface.h"
#include "Grid/Seam_GridCoord.h"
#include "Grid/Seam_TileProviderRead.h"
#include "World/SimGrid_CoordTypes.h"
#include "SimGrid_AutoTileLib.generated.h"

/**
 * Result of a connected-region labeling pass: each cell is assigned a region id (>= 0), with cells in the
 * same 4- or 8-connected blob of "connected" tiles sharing one id. Unmatched cells are absent from the map.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMGRID_API FSimGrid_RegionLabeling
{
	GENERATED_BODY()

	/** Cell -> region id. Cells not in any matched region are absent. */
	TMap<FSeam_CellCoord, int32> RegionIdByCell;

	/** Number of distinct regions found (region ids run [0, RegionCount)). */
	UPROPERTY(BlueprintReadOnly, Category = "SimGrid|AutoTile")
	int32 RegionCount = 0;

	/** Cell count per region id (index == region id). */
	UPROPERTY(BlueprintReadOnly, Category = "SimGrid|AutoTile")
	TArray<int32> RegionSizes;

	/** True if the pass stopped at the cell-visit cap before fully labeling. */
	UPROPERTY(BlueprintReadOnly, Category = "SimGrid|AutoTile")
	bool bTruncated = false;
};

/**
 * Pure, stateless helpers for grid AUTO-TILING and adjacency analysis, expressed entirely against the
 * read seam (ISeam_TileProviderRead) so they work over any grid. Everything is static and side-effect
 * free, safe on clients and in hot loops. Bounded by USimGrid_FeatureSettings (MaxLabelRegionCells) where
 * a pass could otherwise visit an unbounded area.
 *
 * "Connected" means: two cells join for auto-tiling when their tile types match — either the exact
 * tile-type tag, or a shared AutoTileCategory on USimGrid_TileTypeDefinition (so all road variants bevel
 * into each other). The predicate is resolved once per query from the centre cell's category.
 *
 * Provides the three classic primitives:
 *  - ComputeAdjacencyBitmask: the 4- or 8-bit "which neighbours are connected" mask used to pick a
 *    blob/wang visual index (the standard 16- or 256-entry auto-tile rule).
 *  - LabelConnectedRegions: connected-component labeling (flood fill) over matched cells.
 *  - MarchingSquaresCase: the 4-bit marching-squares case for a 2x2 cell corner, for contour/edge meshes.
 */
UCLASS()
class DESIGNPATTERNSSIMGRID_API USimGrid_AutoTileLib : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * The adjacency bitmask of Cell on Grid under Adjacency. For Four-connectivity bits are
	 * 1=+X 2=-X 4=+Y 8=-Y (a 0..15 value). For Eight-connectivity the four diagonal bits 16..128 are
	 * appended, BUT a diagonal bit is only set when BOTH its flanking cardinals are also connected (the
	 * usual "blob" rule that prevents lone diagonal joins) — giving the 47-tile reduced wang set behaviour.
	 * A neighbour counts as connected when ConnectsTo(Grid, Cell, Neighbour) is true.
	 */
	UFUNCTION(BlueprintCallable, Category = "SimGrid|AutoTile")
	static int32 ComputeAdjacencyBitmask(const TScriptInterface<ISeam_TileProviderRead>& Grid,
		const FSeam_CellCoord& Cell, ESimGrid_Adjacency Adjacency);

	/**
	 * True if Neighbour is "connected" to Cell for auto-tiling: both cells are Set, and either they share
	 * the exact tile-type tag or a non-empty AutoTileCategory (resolved via the data registry). Unknown
	 * cells are never connected (a client treats unreplicated cells as gaps).
	 */
	UFUNCTION(BlueprintCallable, Category = "SimGrid|AutoTile")
	static bool ConnectsTo(const TScriptInterface<ISeam_TileProviderRead>& Grid,
		const FSeam_CellCoord& Cell, const FSeam_CellCoord& Neighbour);

	/**
	 * Connected-component labeling over the cells in the inclusive window [Min, Max] that connect to
	 * MatchCategory. Two cells join when both are Set and share MatchCategory (or, if MatchCategory is
	 * invalid, when they share their exact tile-type tag). Bounded by MaxLabelRegionCells; sets bTruncated
	 * if the cap is hit. Uses the given adjacency for connectivity.
	 */
	UFUNCTION(BlueprintCallable, Category = "SimGrid|AutoTile")
	static FSimGrid_RegionLabeling LabelConnectedRegions(const TScriptInterface<ISeam_TileProviderRead>& Grid,
		const FSeam_CellCoord& Min, const FSeam_CellCoord& Max, FGameplayTag MatchCategory,
		ESimGrid_Adjacency Adjacency);

	/**
	 * The 4-bit marching-squares case (0..15) for the 2x2 corner whose bottom-left cell is Corner. Bit 1
	 * = Corner, bit 2 = +X, bit 4 = +X+Y, bit 8 = +Y, each set when that cell is Set (matched against
	 * MatchCategory like ConnectsTo). Drives contour/edge mesh selection along the boundary of a region.
	 */
	UFUNCTION(BlueprintCallable, Category = "SimGrid|AutoTile")
	static int32 MarchingSquaresCase(const TScriptInterface<ISeam_TileProviderRead>& Grid,
		const FSeam_CellCoord& Corner, FGameplayTag MatchCategory);
};
