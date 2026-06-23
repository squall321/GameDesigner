// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Grid/Seam_GridCoord.h"
#include "Seam_LayeredCellCoord.generated.h"

/**
 * A multi-layer decorator over FSeam_CellCoord: a flat 2D cell coordinate plus a discrete layer index.
 *
 * Owned by the Seams module (next to FSeam_CellCoord) so a multi-layer grid model and its read-only
 * consumers compose without depending on the SimGrid module. Layered grids stack several independent
 * 2D planes (e.g. ground / sub-level / overhang, or floors of a building) under one coordinate space;
 * the Layer index selects the plane while the Cell selects the (X,Y) within it.
 *
 * Each layered coordinate projects losslessly to and from a flat FSeam_CellCoord at a given layer, so
 * any consumer that only understands flat cells (existing ISeam_TileProviderRead, pathing, fog) operates
 * on a single layer at a time by projecting. Hashable and comparable for use as a TMap key.
 *
 * NOTE: this is purely a coordinate type — it adds NO behaviour to the flat grid. Cross-layer traversal
 * (ramps / stairs between planes) is a separate concern that a layered provider models explicitly; this
 * type only names a cell within a layer.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSEAMS_API FSeam_LayeredCellCoord
{
	GENERATED_BODY()

	/** The flat 2D cell within the selected layer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Seam|Grid")
	FSeam_CellCoord Cell;

	/**
	 * Which stacked plane this coordinate addresses. Layer 0 is the base/ground plane, which a flat
	 * consumer sees by default. Signed so a project can model below-ground planes as negative layers.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Seam|Grid")
	int16 Layer = 0;

	FSeam_LayeredCellCoord() = default;
	explicit FSeam_LayeredCellCoord(const FSeam_CellCoord& InCell, int16 InLayer = 0)
		: Cell(InCell), Layer(InLayer) {}
	FSeam_LayeredCellCoord(int32 InX, int32 InY, int16 InLayer = 0)
		: Cell(InX, InY), Layer(InLayer) {}

	/** The flat 2D cell, discarding the layer (for handing to flat consumers). */
	FSeam_CellCoord ToFlat() const { return Cell; }

	/** Build a layered coordinate from a flat cell at the given layer. */
	static FSeam_LayeredCellCoord FromFlat(const FSeam_CellCoord& InCell, int16 InLayer = 0)
	{
		return FSeam_LayeredCellCoord(InCell, InLayer);
	}

	bool operator==(const FSeam_LayeredCellCoord& Other) const
	{
		return Cell == Other.Cell && Layer == Other.Layer;
	}
	bool operator!=(const FSeam_LayeredCellCoord& Other) const { return !(*this == Other); }

	friend uint32 GetTypeHash(const FSeam_LayeredCellCoord& C)
	{
		return HashCombine(GetTypeHash(C.Cell), ::GetTypeHash(C.Layer));
	}

	FString ToString() const
	{
		return FString::Printf(TEXT("L%d:%s"), static_cast<int32>(Layer), *Cell.ToString());
	}
};
