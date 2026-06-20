// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Grid/Seam_GridCoord.h"
#include "Seam_TileProviderRead.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_TileProviderRead : public UInterface
{
	GENERATED_BODY()
};

/**
 * Read-only grid seam. The SimGrid world subsystem implements this; consumers that only need to read
 * the grid (agent steering, economy facility placement checks) depend on this seam instead of SimGrid,
 * so they compose even when SimGrid is swapped or absent. All methods are const and client-safe; reads
 * return a tri-state snapshot so a client distinguishes "not replicated" from "empty".
 */
class DESIGNPATTERNSSEAMS_API ISeam_TileProviderRead
{
	GENERATED_BODY()

public:
	/** Convert a world location to its cell coordinate. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Grid")
	FSeam_CellCoord WorldToCell(const FVector& WorldLocation) const;

	/** Center (or corner) world location of a cell. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Grid")
	FVector CellToWorld(const FSeam_CellCoord& Cell, bool bCenter) const;

	/** True if Cell is within the configured grid bounds. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Grid")
	bool IsValidCell(const FSeam_CellCoord& Cell) const;

	/** Tri-state snapshot of a cell (Unknown/Empty/Set + tile type). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Grid")
	FSeam_CellSnapshot GetCellSnapshot(const FSeam_CellCoord& Cell) const;

	/** The grid's cell size in world units (for consumers doing their own spacing math). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Grid")
	float GetCellSize() const;
};
