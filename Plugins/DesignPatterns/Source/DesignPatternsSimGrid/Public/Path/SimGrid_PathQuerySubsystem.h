// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "UObject/ScriptInterface.h"
#include "Grid/Seam_GridCoord.h"
#include "Grid/Seam_TileProviderRead.h"
#include "Path/SimGrid_PathTypes.h"
#include "SimGrid_PathQuerySubsystem.generated.h"

class ISeam_HeightProvider;
class UDP_DataRegistrySubsystem;

/**
 * World-scoped grid pathfinding service. Computes A* paths over WALKABLE cells of any grid that exposes
 * ISeam_TileProviderRead, with per-tile-type entry cost (resolved from USimGrid_TileTypeDefinition via
 * the data registry), 4/8-connected movement, configurable diagonal cost + corner-cut rule, and optional
 * slope cost from an ISeam_HeightProvider. Every search is hard-bounded by USimGrid_FeatureSettings
 * (MaxPathNodes) so a single request can never spin the game thread.
 *
 * Holds NO authoritative state and never mutates the grid — it is a pure read query, so it is client-safe
 * (a client paths over the cells it has replicated; Unknown cells are treated as walls so a client path
 * never leaks across unreplicated regions). The grid to query is resolved once from the service locator
 * (the SimGrid world subsystem registers under DP.Service.SimGrid.TileProvider) and cached weakly per
 * call; callers may also pass an explicit grid.
 *
 * Path RESULTS are cached by (start, goal, layer, adjacency) in a sibling USimGrid_PathCacheSubsystem,
 * which invalidates entries when a cell along a cached path changes (observer-driven dynamic re-route).
 */
UCLASS()
class DESIGNPATTERNSSIMGRID_API USimGrid_PathQuerySubsystem : public UDP_WorldSubsystem
{
	GENERATED_BODY()

public:
	/** Read-only and client-safe; provided for parity with sibling subsystems / debug strings. */
	bool HasWorldAuthority() const
	{
		const UWorld* W = GetWorld();
		return W && W->GetNetMode() != NM_Client;
	}

	/**
	 * Compute a path for Request against the grid resolved from the service locator. Consults the path
	 * cache first; on a miss runs A* and (when caching is enabled) stores the result. Always returns a
	 * fully-populated FSimGrid_PathResult (never throws); inspect Result for the outcome.
	 */
	UFUNCTION(BlueprintCallable, Category = "SimGrid|Path")
	FSimGrid_PathResult FindPath(const FSimGrid_PathRequest& Request);

	/**
	 * Compute a path against an EXPLICIT grid (bypasses locator resolution). Does NOT consult or populate
	 * the cache — use for one-off queries over a transient/alternate grid.
	 */
	FSimGrid_PathResult FindPathOnGrid(const TScriptInterface<ISeam_TileProviderRead>& Grid,
		const FSimGrid_PathRequest& Request) const;

	/**
	 * True if a single cell is walkable on Grid: its snapshot must be KNOWN (Unknown counts as a wall on
	 * clients) and either empty or a tile type whose definition has bWalkable. Public so cache/flow
	 * consumers share one walkability rule.
	 */
	bool IsCellWalkable(const TScriptInterface<ISeam_TileProviderRead>& Grid,
		const FSeam_CellCoord& Cell) const;

	/**
	 * Entry cost of a walkable cell on Grid: the tile type's TraversalCost when > 0, else the request's
	 * default-cost override when > 0, else the settings default. Always > 0 for a walkable cell.
	 */
	float GetCellEntryCost(const TScriptInterface<ISeam_TileProviderRead>& Grid,
		const FSeam_CellCoord& Cell, float DefaultCostOverride) const;

	/** Resolve the grid provider seam from the service locator (cached weakly, re-resolved on staleness). */
	TScriptInterface<ISeam_TileProviderRead> ResolveGrid() const;

	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	//~ Begin UDP_WorldSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_WorldSubsystem

private:
	/** Cached service tag the grid provider is published under (snapshotted from the layout settings). */
	UPROPERTY(Transient)
	FGameplayTag GridProviderServiceTag;

	/** Cached grid provider, re-resolved through the locator when the weak object goes stale. */
	mutable TWeakObjectPtr<UObject> CachedGridObject;

	/** Cached height provider for slope cost (optional; null when no provider is registered). */
	mutable TWeakObjectPtr<UObject> CachedHeightObject;

	/** Number of A* searches run this session (for the debug string). */
	mutable int32 SearchCount = 0;

	/** Resolve the height provider seam from the locator (optional), or an empty interface. */
	TScriptInterface<ISeam_HeightProvider> ResolveHeightProvider() const;

	/** True if a diagonal step from From to To is legal given the corner-cut setting (orthogonal cells). */
	bool DiagonalAllowed(const TScriptInterface<ISeam_TileProviderRead>& Grid,
		const FSeam_CellCoord& From, const FSeam_CellCoord& To, bool bAllowCornerCut) const;
};
