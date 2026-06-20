// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Grid/Seam_GridCoord.h"
#include "SimGrid_GridObserver.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USimGrid_GridObserver : public UInterface
{
	GENERATED_BODY()
};

/**
 * Implemented by systems that want to be told when a grid cell's contents change (a placement landed,
 * a tile was cleared, ownership changed). The authoritative grid carrier (and its OnRep handlers on
 * clients) notify registered observers so they can react — e.g. a flow-field rebuild, a fog reveal, an
 * economy facility re-scan — WITHOUT the carrier hard-depending on those systems.
 *
 * Observers are non-owning: the carrier holds them as TWeakInterfacePtr and null-checks before each
 * call. All callbacks fire on both server and clients (clients via replication), so observers must not
 * assume authority inside them; mutate authoritative state only behind an authority guard.
 */
class DESIGNPATTERNSSIMGRID_API ISimGrid_GridObserver
{
	GENERATED_BODY()

public:
	/**
	 * A single cell's contents changed. NewTileType is the cell's tile-type tag after the change (an
	 * invalid tag means the cell was cleared to empty).
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "SimGrid|Observer")
	void OnCellChanged(const FSeam_CellCoord& Cell, const FGameplayTag& NewTileType);

	/**
	 * A rectangular block of cells changed at once (e.g. a multi-cell placement or a chunk reload),
	 * so observers can batch-rebuild instead of handling each cell. Min/Max are inclusive bounds.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "SimGrid|Observer")
	void OnRegionChanged(const FSeam_CellCoord& Min, const FSeam_CellCoord& Max);

	/**
	 * Ownership of a cell changed. NewOwnerId is the owning identity tag after the change (an invalid
	 * tag means the cell became unowned/neutral).
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "SimGrid|Observer")
	void OnCellOwnershipChanged(const FSeam_CellCoord& Cell, const FGameplayTag& NewOwnerId);
};
