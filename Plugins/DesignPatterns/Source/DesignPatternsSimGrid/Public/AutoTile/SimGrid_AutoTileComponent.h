// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UObject/ScriptInterface.h"
#include "Grid/Seam_GridCoord.h"
#include "Grid/Seam_TileProviderRead.h"
#include "World/SimGrid_CoordTypes.h"
#include "SimGrid_AutoTileComponent.generated.h"

class USimGrid_AutoTileSet;
class ASimGrid_ChunkReplicator;

/**
 * Fired when a cell's resolved auto-tile visual index changes, so a renderer can swap that cell's variant.
 * Cosmetic and local — fires on whatever machine the component lives on (server and clients alike, driven
 * by replicated cell changes), never replicated itself.
 * @param Cell        The cell whose visual index changed.
 * @param VisualIndex The newly-resolved visual index from the matching auto-tile set.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSimGrid_OnAutoTileVisualChanged,
	FSeam_CellCoord, Cell, int32, VisualIndex);

/**
 * Per-actor component that maintains AUTO-TILE visual indices for a window of grid cells and notifies a
 * renderer when they change. Purely COSMETIC: it computes nothing authoritative, holds no replicated
 * state, and never mutates the grid — it reads the grid through ISeam_TileProviderRead and the auto-tile
 * library, then broadcasts visual-index updates.
 *
 * It listens to chunk-carrier cell changes (server and clients) and, on each affected cell, recomputes
 * that cell's adjacency bitmask + its connected neighbours' bitmasks (a cell change can re-bevel its
 * neighbours) and re-resolves their visual indices through the auto-tile set whose category matches.
 *
 * The watched window and the set(s) are designer-configured; nothing is hardcoded. Attach to any actor
 * that owns a tile renderer for a grid region.
 */
UCLASS(ClassGroup = (SimGrid), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSSIMGRID_API USimGrid_AutoTileComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USimGrid_AutoTileComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent

	/** Recompute every cell's visual index across the watched window from scratch and broadcast changes. */
	UFUNCTION(BlueprintCallable, Category = "SimGrid|AutoTile")
	void RefreshAll();

	/** The most recently resolved visual index for Cell, or DefaultVisualIndex of the matching set / -1. */
	UFUNCTION(BlueprintPure, Category = "SimGrid|AutoTile")
	int32 GetVisualIndex(const FSeam_CellCoord& Cell) const;

	/** Broadcast when a cell's resolved visual index changes. */
	UPROPERTY(BlueprintAssignable, Category = "SimGrid|AutoTile")
	FSimGrid_OnAutoTileVisualChanged OnAutoTileVisualChanged;

	/** Receiver for a chunk carrier's OnCellChanged delegate; re-tiles the cell and its neighbours. */
	UFUNCTION()
	void HandleCarrierCellChanged(ASimGrid_ChunkReplicator* Carrier, FSeam_CellCoord Coord);

protected:
	/** Inclusive minimum corner of the cell window this component auto-tiles. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimGrid|AutoTile")
	FSeam_CellCoord WindowMin = FSeam_CellCoord(-64, -64);

	/** Inclusive maximum corner of the cell window this component auto-tiles. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimGrid|AutoTile")
	FSeam_CellCoord WindowMax = FSeam_CellCoord(64, 64);

	/** Connectivity used for bitmask computation (Eight enables corner beveling). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimGrid|AutoTile")
	ESimGrid_Adjacency Adjacency = ESimGrid_Adjacency::Eight;

	/** Auto-tile sets this component can apply, indexed at runtime by their AutoTileCategory. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimGrid|AutoTile")
	TArray<TObjectPtr<USimGrid_AutoTileSet>> AutoTileSets;

private:
	/** Last-broadcast visual index per cell, so we only fire the delegate on an actual change. */
	TMap<FSeam_CellCoord, int32> VisualIndexByCell;

	/** Resolve the grid provider seam from the service locator (cached weakly). */
	TScriptInterface<ISeam_TileProviderRead> ResolveGrid() const;

	/** Cached grid object for the seam, re-resolved on staleness. */
	mutable TWeakObjectPtr<UObject> CachedGridObject;

	/** Find the auto-tile set whose category matches a cell, or null. */
	USimGrid_AutoTileSet* FindSetForCategory(const FGameplayTag& Category) const;

	/** Recompute and (if changed) broadcast the visual index for a single cell. */
	void RetileCell(const TScriptInterface<ISeam_TileProviderRead>& Grid, const FSeam_CellCoord& Cell);

	/** True if Cell lies within the watched window. */
	bool InWindow(const FSeam_CellCoord& Cell) const;

	/** Bind the OnCellChanged delegate of every currently-known chunk carrier (idempotent). */
	void BindCarrierDelegates();
};
