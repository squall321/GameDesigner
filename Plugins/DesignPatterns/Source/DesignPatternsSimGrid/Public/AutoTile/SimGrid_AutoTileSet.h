// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "SimGrid_AutoTileSet.generated.h"

/**
 * Data-driven mapping from an adjacency BITMASK (as produced by USimGrid_AutoTileLib::ComputeAdjacencyBitmask)
 * to a VISUAL INDEX a renderer uses to pick the matching tile sprite/mesh variant (the "wang set" table).
 *
 * Identity is the inherited UDP_DataAsset::DataTag (e.g. SimGrid.AutoTile.Road), and the asset is bound to
 * an AutoTileCategory so the auto-tile component knows which set to consult for a given cell. The bitmask
 * -> index table is authored explicitly (no magic numbers in code); an unmapped mask falls back to
 * DefaultVisualIndex so a partially-authored set still renders something sane.
 *
 * Collapses every auto-tile set into one asset-manager bucket ("SimGrid_AutoTileSet") so a game can
 * enumerate all sets as a single PrimaryAssetType regardless of subclass.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSSIMGRID_API USimGrid_AutoTileSet : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/**
	 * The auto-tile category this set styles (matches USimGrid_TileTypeDefinition::AutoTileCategory). The
	 * auto-tile component selects this set for a cell whose category matches.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimGrid|AutoTile")
	FGameplayTag AutoTileCategory;

	/**
	 * Whether this set was authored for 4-connected (16-entry) or 8-connected (up to 256-entry, typically
	 * the reduced 47) bitmasks. Used to validate that callers pass a compatible mask.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimGrid|AutoTile")
	bool bEightConnected = true;

	/**
	 * Bitmask -> visual index table. Keys are the masks from ComputeAdjacencyBitmask; values index into
	 * the renderer's variant array. Authored per project; no defaults baked into code.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimGrid|AutoTile")
	TMap<int32, int32> VisualIndexByBitmask;

	/** Visual index returned when a queried bitmask is not in the table. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimGrid|AutoTile", meta = (ClampMin = "0"))
	int32 DefaultVisualIndex = 0;

	/** Resolve a bitmask to its visual index, falling back to DefaultVisualIndex when unmapped. */
	UFUNCTION(BlueprintCallable, Category = "SimGrid|AutoTile")
	int32 ResolveVisualIndex(int32 Bitmask) const
	{
		if (const int32* Found = VisualIndexByBitmask.Find(Bitmask))
		{
			return *Found;
		}
		return DefaultVisualIndex;
	}

	//~ Begin UDP_DataAsset
	/** Collapse all auto-tile sets into one asset-manager bucket regardless of subclass. */
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset
};
