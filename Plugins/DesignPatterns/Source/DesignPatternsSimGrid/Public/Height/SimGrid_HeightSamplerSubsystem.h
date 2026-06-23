// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "Grid/Seam_GridCoord.h"
#include "Grid/Seam_HeightProvider.h"
#include "Interfaces/SimGrid_GridObserver.h"

#include "SimGrid_HeightSamplerSubsystem.generated.h"

class ASimGrid_ChunkReplicator;

/**
 * World-scoped service that provides per-cell terrain HEIGHT by running downward line traces.
 *
 * DESIGN OVERVIEW
 *  - Heights are computed LAZILY on first access via SampleCellHeight and cached in HeightCache.
 *    SampleCellHeightNow bypasses the cache and always runs a fresh trace.
 *  - The cache is keyed by FSeam_CellCoord. A cached height is the world-Z (cm) at the cell's
 *    centre point after a downward trace hits a WorldStatic surface.
 *  - Trace config (channel, start Z, length, fallback Z) comes from USimGrid_FeatureSettings so
 *    every project can tune sampling without recompiling.
 *  - Cell centre XY is queried through ISeam_TileProviderRead::Execute_CellToWorld on the world
 *    subsystem (bCenter=true), so the height sample is geometrically consistent with the grid.
 *
 * CACHE INVALIDATION
 *  Implements ISimGrid_GridObserver and binds to each chunk carrier's OnCellChanged delegate at
 *  Initialize (same BindCarrierDelegates pattern as USimGrid_AutoTileComponent). When a cell changes
 *  its height entry is evicted so the next read triggers a fresh trace. OnRegionChanged evicts the
 *  whole rectangular region; OnCellOwnershipChanged is a no-op (height is terrain, not ownership).
 *
 * SERVICE LOCATOR
 *  Registers itself as WeakObserved under SimGridTags::Service_HeightProvider (falling back to the
 *  feature settings HeightProviderServiceTag) on Initialize; unregisters on Deinitialize.
 *
 * Implements ISeam_HeightProvider and ISimGrid_GridObserver (BlueprintNativeEvent).
 */
UCLASS()
class DESIGNPATTERNSSIMGRID_API USimGrid_HeightSamplerSubsystem
	: public UDP_WorldSubsystem
	, public ISeam_HeightProvider
	, public ISimGrid_GridObserver
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

	// --- Public API -----------------------------------------------------------------------

	/**
	 * Sample the terrain height at the centre of Cell, bypassing the cache.
	 * Always runs a fresh downward line trace. The cache is NOT updated (use SampleCellHeight for
	 * caching behaviour). Returns HeightFallbackZ when no surface is hit.
	 */
	UFUNCTION(BlueprintCallable, Category = "SimGrid|Height")
	float SampleCellHeightNow(const FSeam_CellCoord& Cell) const;

	/** Evict a single cell from the height cache so the next read triggers a fresh trace. */
	UFUNCTION(BlueprintCallable, Category = "SimGrid|Height")
	void InvalidateCell(const FSeam_CellCoord& Cell);

	/** Clear the entire height cache. The next read of any cell will trace. */
	UFUNCTION(BlueprintCallable, Category = "SimGrid|Height")
	void ClearCache();

	// --- ISeam_HeightProvider (BlueprintNativeEvent) -------------------------------------

	/**
	 * Ground height (world Z, cm) at the centre of Cell. Checks the cache first; runs a trace and
	 * populates the cache if the cell has not been sampled. bOutValid is false when no surface was
	 * found and the fallback Z is returned; true for a real surface height.
	 */
	virtual float SampleCellHeight_Implementation(const FSeam_CellCoord& Cell, bool& bOutValid) const override;

	/**
	 * Signed height difference (cm) from cell A's ground to cell B's ground (B - A). Falls back to
	 * 0 when either cell has no sampled height.
	 */
	virtual float GetHeightDelta_Implementation(const FSeam_CellCoord& A, const FSeam_CellCoord& B) const override;

	/** True if a sampled (non-fallback) height is cached for Cell. */
	virtual bool HasSampledHeight_Implementation(const FSeam_CellCoord& Cell) const override;

	// --- ISimGrid_GridObserver (BlueprintNativeEvent) ------------------------------------

	/** Evict the changed cell from the height cache. */
	virtual void OnCellChanged_Implementation(const FSeam_CellCoord& Cell, const FGameplayTag& NewTileType) override;

	/** Evict all cells in the changed region from the height cache. */
	virtual void OnRegionChanged_Implementation(const FSeam_CellCoord& Min, const FSeam_CellCoord& Max) override;

	/** No-op: height is terrain, not ownership. */
	virtual void OnCellOwnershipChanged_Implementation(const FSeam_CellCoord& Cell, const FGameplayTag& NewOwnerId) override;

	/** UFUNCTION handler bound to each chunk carrier's OnCellChanged delegate. */
	UFUNCTION()
	void HandleCarrierCellChanged(ASimGrid_ChunkReplicator* Carrier, FSeam_CellCoord Coord);

	//~ Begin UDP_WorldSubsystem
	/** Returns a one-line summary: cache size. */
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_WorldSubsystem

private:
	/**
	 * Height cache: world Z (cm) at each sampled cell's centre, populated on demand.
	 * Marked mutable because SampleCellHeight_Implementation (const seam contract) writes it.
	 * Only valid (non-fallback) heights are stored; a cache miss means trace has not run or hit nothing.
	 */
	mutable TMap<FSeam_CellCoord, float> HeightCache;

	/**
	 * Tracks which cells have a VALID (non-fallback) height in the cache. A cell absent from HeightCache
	 * has not been sampled; a cell absent from ValidCells but present in HeightCache holds the fallback.
	 * Using a separate set avoids storing a bool alongside each float.
	 */
	mutable TSet<FSeam_CellCoord> ValidCells;

	/** Service tag registered under; kept for clean unregister. */
	UPROPERTY(Transient)
	FGameplayTag RegisteredServiceTag;

	// --- Cached trace settings (snapshotted from USimGrid_FeatureSettings at Initialize) ---

	/** Collision channel for the downward height trace. */
	TEnumAsByte<ECollisionChannel> HeightTraceChannel = ECC_WorldStatic;

	/** World Z (cm) the trace starts from. */
	float HeightTraceStartZ = 100000.f;

	/** Length (cm) of the downward trace. */
	float HeightTraceLength = 200000.f;

	/** Z (cm) reported when no surface is hit. */
	float HeightFallbackZ = 0.f;

	/** Register under the height provider service tag as WeakObserved. */
	void RegisterAsHeightProvider();

	/** Bind to every currently-known chunk carrier's OnCellChanged delegate (idempotent). */
	void BindCarrierDelegates();

	/**
	 * Internal trace helper: runs a single downward trace from the cell's centre XY at HeightTraceStartZ
	 * going down by HeightTraceLength. Returns the hit Z on success, HeightFallbackZ otherwise.
	 * bOutValid is set to true only when a surface was hit.
	 */
	float RunHeightTrace(const FSeam_CellCoord& Cell, bool& bOutValid) const;
};
