// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "Grid/Seam_GridCoord.h"
#include "Interfaces/SimGrid_GridObserver.h"
#include "Path/SimGrid_PathTypes.h"
#include "SimGrid_PathCacheSubsystem.generated.h"

class ASimGrid_ChunkReplicator;
class USimGrid_WorldSubsystem;

/**
 * World-scoped LRU cache of computed grid paths with observer-driven invalidation (dynamic re-route).
 *
 * USimGrid_PathQuerySubsystem stores each successful path here keyed by (start, goal, layer, adjacency).
 * When ANY cell along a cached path changes (a wall placed, a door cleared), the cache drops that path so
 * the next request re-plans — agents automatically re-route around new obstacles without polling.
 *
 * Invalidation is driven two ways, both wired here so the query subsystem stays a pure read:
 *  - This subsystem implements ISimGrid_GridObserver; any system that already notifies grid observers
 *    forwards cell/region changes to it.
 *  - It also binds the chunk carriers' OnCellChanged/OnOwnershipChanged dynamic delegates as carriers are
 *    discovered, so it reacts even without a central observer registry.
 *
 * The cache holds NO authoritative state and is fully client-safe. Cache size comes from
 * USimGrid_FeatureSettings::PathCacheSize (0 disables caching entirely).
 */
UCLASS()
class DESIGNPATTERNSSIMGRID_API USimGrid_PathCacheSubsystem : public UDP_WorldSubsystem, public ISimGrid_GridObserver
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/**
	 * Look up a cached path for Request. Returns true and fills OutResult on a hit; false on a miss or
	 * when caching is disabled. A hit promotes the entry to most-recently-used.
	 */
	bool TryGetCachedPath(const FSimGrid_PathRequest& Request, FSimGrid_PathResult& OutResult);

	/**
	 * Store a successful path for Request, evicting the least-recently-used entry if the cache is full.
	 * No-op when caching is disabled or Result is not a valid path.
	 */
	void StorePath(const FSimGrid_PathRequest& Request, const FSimGrid_PathResult& Result);

	/** Drop every cached entry whose path passes through Cell (a cell changed). */
	void InvalidateCellPaths(const FSeam_CellCoord& Cell);

	/** Drop every cached entry whose path intersects the inclusive rectangle [Min, Max]. */
	void InvalidateRegionPaths(const FSeam_CellCoord& Min, const FSeam_CellCoord& Max);

	/** Clear the entire cache (e.g. on grid reset). */
	UFUNCTION(BlueprintCallable, Category = "SimGrid|Path")
	void ClearCache();

	/** Number of paths currently cached. */
	UFUNCTION(BlueprintPure, Category = "SimGrid|Path")
	int32 GetCachedPathCount() const { return Entries.Num(); }

	//~ Begin ISimGrid_GridObserver (forwarded invalidation; fire on server and clients)
	virtual void OnCellChanged_Implementation(const FSeam_CellCoord& Cell, const FGameplayTag& NewTileType) override;
	virtual void OnRegionChanged_Implementation(const FSeam_CellCoord& Min, const FSeam_CellCoord& Max) override;
	virtual void OnCellOwnershipChanged_Implementation(const FSeam_CellCoord& Cell, const FGameplayTag& NewOwnerId) override;
	//~ End ISimGrid_GridObserver

	//~ Begin UDP_WorldSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_WorldSubsystem

	/** Receiver for a chunk carrier's OnCellChanged delegate; invalidates the affected cell's paths. */
	UFUNCTION()
	void HandleCarrierCellChanged(ASimGrid_ChunkReplicator* Carrier, FSeam_CellCoord Coord);

private:
	/** Identity of a cached request (cells + layer + adjacency). Hashable for use as the cache key. */
	struct FPathKey
	{
		FSeam_CellCoord Start;
		FSeam_CellCoord Goal;
		int32 Layer = 0;
		uint8 Adjacency = 0;

		bool operator==(const FPathKey& O) const
		{
			return Start == O.Start && Goal == O.Goal && Layer == O.Layer && Adjacency == O.Adjacency;
		}
		friend uint32 GetTypeHash(const FPathKey& K)
		{
			uint32 H = HashCombine(GetTypeHash(K.Start), GetTypeHash(K.Goal));
			H = HashCombine(H, ::GetTypeHash(K.Layer));
			return HashCombine(H, ::GetTypeHash(K.Adjacency));
		}
	};

	/** A cached path plus a set of the cells it occupies, for O(1) invalidation membership tests. */
	struct FCacheEntry
	{
		FSimGrid_PathResult Result;
		TSet<FSeam_CellCoord> OccupiedCells;
		uint64 LastUsedSerial = 0;
	};

	static FPathKey MakeKey(const FSimGrid_PathRequest& Request);

	/** key -> entry. */
	TMap<FPathKey, FCacheEntry> Entries;

	/** Monotonic serial bumped on each access, used as the LRU recency stamp. */
	uint64 AccessSerial = 0;

	/** Evict the least-recently-used entries until the cache is within Capacity. */
	void EnforceCapacity(int32 Capacity);

	/** Bind the OnCellChanged delegate of every currently-known chunk carrier (idempotent). */
	void BindCarrierDelegates();

	/** The world subsystem we discover carriers from (re-resolved on use; never owned). */
	USimGrid_WorldSubsystem* ResolveGridWorld() const;
};
