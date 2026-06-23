// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"

// FInstancedStruct lives in StructUtils on UE 5.3/5.4, merged into CoreUObject in 5.5.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "SimGrid_TileTypeDefinition.generated.h"

class UStaticMesh;

/**
 * Tag-identified definition of one grid tile type (grass, wall, road, water...).
 *
 * Identity is the inherited UDP_DataAsset::DataTag; placed cells store only that tag
 * (FSimGrid_CellEntry::TileTypeTag) and resolve the definition through the core data registry, so
 * cells stay light and serialize/replicate trivially. The definition carries authoring-time
 * properties (buildable/walkable flags, terrain categories), a preview mesh, and a template payload
 * that newly-placed cells of this type are seeded with.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSSIMGRID_API USimGrid_TileTypeDefinition : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/**
	 * Coarse terrain classifications this tile belongs to (e.g. SimGrid.Terrain.Land,
	 * SimGrid.Terrain.Water). Consumers filter/score placement by hierarchy match against these.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimGrid|Tile")
	FGameplayTagContainer TerrainCategories;

	/** Whether structures may be placed on cells of this tile type. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimGrid|Tile")
	bool bBuildable = true;

	/** Whether agents may traverse cells of this tile type. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimGrid|Tile")
	bool bWalkable = true;

	/**
	 * Per-cell movement cost a grid pathfinder pays to ENTER a cell of this tile type (mud/road/etc.).
	 * Higher means slower/more-costly to cross. A value <= 0 means "use the path settings' default
	 * traversal cost"; walkability is decided by bWalkable, not by this value, so a value of 0 does NOT
	 * make a cell impassable. ADDITIVE field — existing assets default to 0 and behave exactly as before
	 * (pathing uses the default cost for every walkable cell).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimGrid|Tile", meta = (ClampMin = "0.0"))
	float TraversalCost = 0.f;

	/**
	 * Auto-tiling bucket for adjacency/bitmask resolution: two cells are considered "connected" for the
	 * marching-squares / bitmask auto-tiler when their tile types share this category (e.g. all road
	 * variants share SimGrid.AutoTile.Road so they bevel/join into each other). Empty (default) means the
	 * cell only connects to its own exact tile-type tag. ADDITIVE — existing assets default to empty and
	 * keep their prior behaviour.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimGrid|Tile")
	FGameplayTag AutoTileCategory;

	/**
	 * Per-cell payload template. When a cell of this type is placed without an explicit payload, the
	 * carrier seeds the cell's FInstancedStruct from a copy of this template (e.g. a struct holding
	 * fertility, durability, resource amount). May be empty for tiles with no per-cell state.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimGrid|Tile")
	FInstancedStruct DefaultPayloadTemplate;

	/** Optional mesh used by editor/placement-preview tooling to visualise this tile. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimGrid|Tile")
	TObjectPtr<UStaticMesh> PreviewMesh;

	//~ Begin UDP_DataAsset
	/**
	 * Collapse all tile-type definitions into one asset-manager bucket ("SimGrid_TileType") so a game
	 * can enumerate every tile type as a single PrimaryAssetType regardless of subclass.
	 */
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset
};
