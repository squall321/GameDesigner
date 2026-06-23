// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "Grid/Seam_GridCoord.h"
#include "Grid/Seam_LayeredCellCoord.h"
#include "Grid/Seam_LayeredTileProviderRead.h"

#include "SimGrid_LayerStackSubsystem.generated.h"

class USimGrid_WorldSubsystem;

/**
 * World-scoped service that stacks multiple independent 2D grid layers on top of the base flat grid.
 *
 * DESIGN OVERVIEW
 *  - Layer 0 is the FLAT BASE LAYER: all reads and writes on layer 0 are forwarded transparently to
 *    USimGrid_WorldSubsystem (ISeam_TileProviderRead). This preserves full backward-compatibility — any
 *    existing flat consumer continues to see exactly the same data it always did.
 *  - Layers 1 .. (LayerCount-1) are OVERLAY LAYERS stored locally in LayerData[Layer-1]. Each overlay
 *    layer is an authoritative map of FSeam_CellCoord -> FSeam_CellSnapshot. The authority subsystem
 *    owns these maps; no replication carrier is involved (overlay state must be save/replicated by the
 *    project separately if needed).
 *  - The layer count is read from USimGrid_FeatureSettings::GetSafeMaxLayerCount() at Initialize so the
 *    maximum is project-tunable without recompiling.
 *
 * AUTHORITY RULES
 *  SetLayeredCell and ClearLayeredCell guard HasWorldAuthority() at the top. Clients may read freely.
 *
 * SERVICE LOCATOR
 *  Registers itself as WeakObserved under SimGridTags::Service_LayeredTileProvider (falling back to the
 *  feature settings LayeredTileProviderServiceTag when valid) on Initialize; unregisters on Deinitialize.
 *
 * Implements ISeam_LayeredTileProviderRead (BlueprintNativeEvent).
 */
UCLASS()
class DESIGNPATTERNSSIMGRID_API USimGrid_LayerStackSubsystem
	: public UDP_WorldSubsystem
	, public ISeam_LayeredTileProviderRead
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/**
	 * True on the server / standalone (not a pure net client). World subsystems have no inherited
	 * HasWorldAuthority; we declare our own inline.
	 */
	bool HasWorldAuthority() const
	{
		const UWorld* W = GetWorld();
		return W && W->GetNetMode() != NM_Client;
	}

	// --- Authority mutators ----------------------------------------------------------------

	/**
	 * Place or overwrite a cell on the specified layer. AUTHORITY ONLY.
	 *
	 * Layer 0 writes are forwarded to USimGrid_WorldSubsystem::SetCell. Layer N>0 writes are stored
	 * in LayerData[N-1]. TileTypeTag must be valid. Returns true if a change was applied.
	 */
	UFUNCTION(BlueprintCallable, Category = "SimGrid|Layers")
	bool SetLayeredCell(const FSeam_LayeredCellCoord& Coord, const FGameplayTag& TileTypeTag);

	/**
	 * Clear a cell back to empty on the specified layer. AUTHORITY ONLY.
	 *
	 * Layer 0 clears are forwarded to USimGrid_WorldSubsystem::ClearCell. Layer N>0 clears remove the
	 * entry from LayerData[N-1]. Returns true if a tile was present and removed.
	 */
	UFUNCTION(BlueprintCallable, Category = "SimGrid|Layers")
	bool ClearLayeredCell(const FSeam_LayeredCellCoord& Coord);

	// --- ISeam_LayeredTileProviderRead (BlueprintNativeEvent) --------------------------------

	/** Number of stacked layers: always >= 1 (layer 0 = base flat grid). */
	virtual int32 GetLayerCount_Implementation() const override;

	/**
	 * Tri-state snapshot of the cell on the given layer.
	 * Layer 0 delegates to the world subsystem. Layers 1+ read from LayerData. A layer index outside
	 * [0, LayerCount) returns KnownState==Empty (never Unknown) for stored layers.
	 */
	virtual FSeam_CellSnapshot GetLayeredCellSnapshot_Implementation(const FSeam_LayeredCellCoord& Coord) const override;

	/** True if the layered coordinate has a valid flat cell AND the layer index is in [0, LayerCount). */
	virtual bool IsValidLayeredCell_Implementation(const FSeam_LayeredCellCoord& Coord) const override;

	/** Always 0: layer 0 is the ground/base plane shared with the flat ISeam_TileProviderRead grid. */
	virtual int32 GetBaseLayer_Implementation() const override;

	//~ Begin UDP_WorldSubsystem
	/** Returns a one-line summary: layer count + total cells across all overlay layers. */
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_WorldSubsystem

private:
	/**
	 * Per-overlay-layer cell data. Index 0 of this array is LAYER 1 of the logical stack (layer 0 is
	 * the base flat grid, forwarded to USimGrid_WorldSubsystem, not stored here). Sized to
	 * (GetSafeMaxLayerCount() - 1) at Initialize, so logical layer N maps to LayerData[N-1].
	 *
	 * KnownState in stored snapshots is either Set (entry exists) or Empty (cleared / not present).
	 * Unknown is intentionally not used for overlay layers on the authority; only the base layer
	 * (forwarded to the world subsystem) may report Unknown on clients.
	 */
	TArray<TMap<FSeam_CellCoord, FSeam_CellSnapshot>> LayerData;

	/** Total number of logical layers (including layer 0). Snapshotted from settings at Initialize. */
	int32 CachedLayerCount = 1;

	/** Service tag this subsystem registered under; kept for clean unregister on teardown. */
	UPROPERTY(Transient)
	FGameplayTag RegisteredServiceTag;

	/** Resolve the base world subsystem (layer 0 operations). */
	USimGrid_WorldSubsystem* GetGridSubsystem() const;

	/** Register under the configured service tag as WeakObserved. */
	void RegisterAsLayeredProvider();

	/** True if LogicalLayer is in [0, CachedLayerCount). */
	bool IsLayerInRange(int32 LogicalLayer) const;
};
