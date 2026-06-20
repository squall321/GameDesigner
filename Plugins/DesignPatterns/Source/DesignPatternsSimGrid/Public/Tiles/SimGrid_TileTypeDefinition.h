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
