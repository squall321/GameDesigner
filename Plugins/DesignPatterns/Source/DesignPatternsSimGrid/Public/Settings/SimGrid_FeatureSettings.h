// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Engine/EngineTypes.h"
#include "SimGrid_FeatureSettings.generated.h"

/**
 * Project-wide configuration for the SimGrid DEEP features (pathfinding, auto-tiling, zones, fog,
 * height sampling, multi-layer grids).
 *
 * This is a DELIBERATELY SEPARATE settings class from USimGrid_DeveloperSettings (the module already
 * ships two colliding classes of that name — query/placement caps and layout/service; both are left
 * untouched). All tunables the new feature code references live here so nothing is a hardcoded magic
 * number. A plain UDeveloperSettings with its own static Get(), grouped under the editor "Plugins"
 * category, displayed distinctly so it never visually merges with the legacy settings record.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Design Patterns SimGrid (Features)"))
class DESIGNPATTERNSSIMGRID_API USimGrid_FeatureSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	USimGrid_FeatureSettings();

	/** Group under the editor's "Plugins" category alongside the rest of the plugin's settings. */
	virtual FName GetCategoryName() const override { return FName("Plugins"); }

	/** Convenience accessor for the project's single config instance. Never null in a configured project. */
	static const USimGrid_FeatureSettings* Get();

	// --- Pathfinding (USimGrid_PathQuerySubsystem / USimGrid_FlowFieldObject) ---------------------

	/**
	 * Hard cap on the number of cells the A* / flow-field expansion may pop from its open set before it
	 * gives up and reports failure. Bounds the worst-case cost of a single path request so a goal walled
	 * off behind a vast open area can never spin the game thread.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Pathfinding", meta = (ClampMin = "1"))
	int32 MaxPathNodes = 8192;

	/**
	 * When true, an 8-connected path may cut a corner past two diagonally-adjacent blocked cells; when
	 * false, a diagonal step is only allowed if BOTH orthogonal cells it passes between are walkable
	 * (the usual "no corner cutting" rule for grid movement).
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Pathfinding")
	bool bAllowDiagonalCornerCut = false;

	/**
	 * Multiplier applied to the base step cost for a diagonal move. The classic value is sqrt(2) so a
	 * diagonal costs ~1.414x a cardinal step, keeping path lengths geometrically faithful.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Pathfinding", meta = (ClampMin = "1.0"))
	float DiagonalCostMultiplier = 1.41421356f;

	/**
	 * Traversal cost used for a walkable cell whose tile type either has no positive TraversalCost or has
	 * no resolvable definition. Must be > 0 so every walkable step has a real cost.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Pathfinding", meta = (ClampMin = "0.01"))
	float DefaultTraversalCost = 1.f;

	/**
	 * Extra per-cm cost added per metre of upward height climbed between adjacent cells (read via
	 * ISeam_HeightProvider when available). 0 disables slope cost. Lets paths prefer flatter ground.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Pathfinding", meta = (ClampMin = "0.0"))
	float SlopeCostPerMetre = 0.f;

	/**
	 * Maximum number of fully-computed paths the path cache subsystem retains (LRU). A cached path is
	 * invalidated when any cell along it changes (observer-driven re-route). 0 disables caching.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Pathfinding", meta = (ClampMin = "0"))
	int32 PathCacheSize = 64;

	/**
	 * Sentinel integration cost meaning "unreachable / outside the field". Any computed cost at or above
	 * this is treated as no-path. Exposed so flow-field consumers compare against one shared value
	 * instead of a literal scattered through code.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Pathfinding", meta = (ClampMin = "1.0"))
	float UnreachableCostSentinel = 1.e9f;

	// --- Height sampling (USimGrid_HeightSamplerSubsystem) ----------------------------------------

	/** Collision channel the downward height trace runs against (terrain/landscape channel). */
	UPROPERTY(EditAnywhere, Config, Category = "Height")
	TEnumAsByte<ECollisionChannel> HeightTraceChannel = ECC_WorldStatic;

	/**
	 * World Z (cm) the downward height trace starts from, above the highest expected terrain. The trace
	 * goes straight down from (cellCentreXY, HeightTraceStartZ) for HeightTraceLength.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Height")
	float HeightTraceStartZ = 100000.f;

	/** Length (cm) of the downward height trace below HeightTraceStartZ. */
	UPROPERTY(EditAnywhere, Config, Category = "Height", meta = (ClampMin = "1.0"))
	float HeightTraceLength = 200000.f;

	/** Z (cm) reported for a cell whose downward trace hit nothing (the grid plane). */
	UPROPERTY(EditAnywhere, Config, Category = "Height")
	float HeightFallbackZ = 0.f;

	// --- Fog of war (ASimGrid_FogCarrier / USimGrid_FogRevealComponent) ---------------------------

	/**
	 * Hard cap on a single reveal request's radius in cells, so a runaway reveal radius cannot rasterise
	 * an unbounded number of cells into the fog fast array in one frame.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Fog", meta = (ClampMin = "1"))
	int32 MaxFogRevealRadius = 48;

	/**
	 * When true the fog tracks a per-team "currently visible" mask separate from the persistent
	 * "explored" mask (the classic two-tier fog where explored terrain stays dim and only currently-seen
	 * terrain is bright). When false only the explored mask is kept.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Fog")
	bool bTrackCurrentVisibility = true;

	// --- Auto-tiling / region labeling (USimGrid_AutoTileLib) -------------------------------------

	/**
	 * Hard cap on cells a single connected-region (marching-squares / flood-label) pass may visit, so
	 * labeling a vast contiguous area cannot stall the game thread.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "AutoTile", meta = (ClampMin = "1"))
	int32 MaxLabelRegionCells = 16384;

	// --- Multi-layer grids (USimGrid_LayerStackSubsystem) -----------------------------------------

	/**
	 * Maximum number of stacked layers a layered grid exposes. Layer indices are clamped into
	 * [0, MaxLayerCount). Keeps the layered provider's bounds finite.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Layers", meta = (ClampMin = "1"))
	int32 MaxLayerCount = 8;

	// --- Zones / districts (ASimGrid_ZoneCarrier / USimGrid_ZoneGrowthComponent) ------------------

	/**
	 * Service-locator key the zone carrier registers itself under so painting/growth systems resolve the
	 * authoritative zone carrier without hard-including it. Anchor under DP.Service.SimGrid.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Zones")
	FGameplayTag ZoneCarrierServiceTag;

	/** Service-locator key the fog carrier registry registers under (per-team carriers keyed beneath). */
	UPROPERTY(EditAnywhere, Config, Category = "Fog")
	FGameplayTag FogCarrierServiceTag;

	/** Service-locator key the layered tile provider registers under. */
	UPROPERTY(EditAnywhere, Config, Category = "Layers")
	FGameplayTag LayeredTileProviderServiceTag;

	/** Service-locator key the height sampler registers under. */
	UPROPERTY(EditAnywhere, Config, Category = "Height")
	FGameplayTag HeightProviderServiceTag;

	// --- Validated accessors (no zero/negative reaches runtime math) ------------------------------

	int32 GetSafeMaxPathNodes() const { return FMath::Max(1, MaxPathNodes); }
	float GetSafeDefaultTraversalCost() const { return FMath::Max(0.01f, DefaultTraversalCost); }
	float GetSafeDiagonalMultiplier() const { return FMath::Max(1.f, DiagonalCostMultiplier); }
	int32 GetSafePathCacheSize() const { return FMath::Max(0, PathCacheSize); }
	float GetUnreachableCost() const { return FMath::Max(1.f, UnreachableCostSentinel); }
	int32 GetSafeMaxFogRevealRadius() const { return FMath::Max(1, MaxFogRevealRadius); }
	int32 GetSafeMaxLabelRegionCells() const { return FMath::Max(1, MaxLabelRegionCells); }
	int32 GetSafeMaxLayerCount() const { return FMath::Max(1, MaxLayerCount); }
	float GetSafeHeightTraceLength() const { return FMath::Max(1.f, HeightTraceLength); }
};
