// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Grid/Seam_GridCoord.h"
#include "Grid/Seam_LayeredCellCoord.h"
#include "Seam_LayeredTileProviderRead.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_LayeredTileProviderRead : public UInterface
{
	GENERATED_BODY()
};

/**
 * Read-only seam for a MULTI-LAYER grid: the layered analogue of ISeam_TileProviderRead. A grid model
 * that stacks several independent 2D planes implements this so consumers can read any plane by its layer
 * index, while flat consumers keep using ISeam_TileProviderRead against the base plane unchanged. This is
 * a SEPARATE interface (not a change to the flat seam) so existing flat consumers are completely
 * untouched and a provider may implement either or both.
 *
 * All methods are const and client-safe; reads return the same tri-state FSeam_CellSnapshot as the flat
 * seam, so a client distinguishes "not replicated" from "empty" per layer. Layer indices outside the
 * provider's configured range report KnownState == Empty (the layer cannot hold a tile) rather than
 * asserting.
 */
class DESIGNPATTERNSSEAMS_API ISeam_LayeredTileProviderRead
{
	GENERATED_BODY()

public:
	/** Number of stacked layers this provider exposes (>= 1). Layer indices live in [0, GetLayerCount). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Grid")
	int32 GetLayerCount() const;

	/** Tri-state snapshot of a cell on a specific layer (Unknown/Empty/Set + tile type). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Grid")
	FSeam_CellSnapshot GetLayeredCellSnapshot(const FSeam_LayeredCellCoord& Coord) const;

	/** True if the layered coordinate lies within the provider's bounds AND layer range. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Grid")
	bool IsValidLayeredCell(const FSeam_LayeredCellCoord& Coord) const;

	/**
	 * The layer index a flat consumer (ISeam_TileProviderRead) maps onto — the "default" / ground plane.
	 * Lets a flat reader and a layered reader agree on which plane they share. Usually 0.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Grid")
	int32 GetBaseLayer() const;
};
