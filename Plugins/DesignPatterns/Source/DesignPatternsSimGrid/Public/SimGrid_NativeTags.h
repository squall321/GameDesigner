// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

/**
 * Native anchor tags for the SimGrid module.
 *
 * These are the C++-defined roots/leaves SimGrid relies on at startup: the service-locator key under
 * which the grid (tile provider) and ownership carrier are published, and the placement-failure reason
 * tags the shipped rules emit. Game projects extend the terrain/tile-type hierarchy in their own tag
 * config; SimGrid only anchors what its own code references by name.
 */
namespace SimGridTags
{
	/** Service-locator key for the live ISeam_TileProviderRead grid provider. */
	DESIGNPATTERNSSIMGRID_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_TileProvider);

	/** Service-locator key for the authoritative ownership/territory carrier actor or component. */
	DESIGNPATTERNSSIMGRID_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_TerritoryCarrier);

	// --- Deep-feature service keys (resolved by the new feature subsystems) ---

	/** Service-locator key for the multi-layer read seam (ISeam_LayeredTileProviderRead). */
	DESIGNPATTERNSSIMGRID_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_LayeredTileProvider);

	/** Service-locator key for the per-cell height seam (ISeam_HeightProvider). */
	DESIGNPATTERNSSIMGRID_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_HeightProvider);

	/** Service-locator key for the authoritative zone/district carrier actor. */
	DESIGNPATTERNSSIMGRID_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_ZoneCarrier);

	/** Service-locator key under which per-team fog carriers are published. */
	DESIGNPATTERNSSIMGRID_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_FogCarrier);

	// --- Pathfinding failure reasons (emitted by the path query subsystem) ---

	/** No path exists between the requested start and goal (goal unreachable / walled off). */
	DESIGNPATTERNSSIMGRID_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(PathFail_NoPath);

	/** The start or goal cell was itself not walkable (cannot begin/end there). */
	DESIGNPATTERNSSIMGRID_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(PathFail_BlockedEndpoint);

	/** The search hit the node budget before reaching the goal and gave up. */
	DESIGNPATTERNSSIMGRID_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(PathFail_NodeBudget);

	// --- Zone reason / state leaves ---

	/** A zone-growth attempt was blocked because the candidate cell was occupied/non-buildable. */
	DESIGNPATTERNSSIMGRID_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(ZoneFail_NotBuildable);

	/** A zone-growth attempt was blocked because the candidate cell was outside the zone's owner. */
	DESIGNPATTERNSSIMGRID_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(ZoneFail_NotOwned);

	// --- Fog reason leaves ---

	/** A reveal/conceal request named a team with no carrier (nothing to reveal into). */
	DESIGNPATTERNSSIMGRID_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(FogFail_NoCarrier);

	// --- Placement failure reason tags (emitted by the shipped rules) ---

	/** A footprint cell fell outside the grid bounds. */
	DESIGNPATTERNSSIMGRID_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Fail_OutOfBounds);

	/** A footprint cell was already occupied (Set). */
	DESIGNPATTERNSSIMGRID_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Fail_CellOccupied);

	/** A footprint cell's terrain was not in the allowed set, or was in the blocked set. */
	DESIGNPATTERNSSIMGRID_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Fail_TerrainNotAllowed);

	/** No required-adjacent tile was found next to the footprint. */
	DESIGNPATTERNSSIMGRID_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Fail_MissingAdjacency);

	/** A footprint cell was not owned by the placement's OwnerId (and ownership was required). */
	DESIGNPATTERNSSIMGRID_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Fail_NotOwnedZone);

	/** The local machine lacked replicated grid state to decide (client-side indeterminate). */
	DESIGNPATTERNSSIMGRID_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Fail_StateUnknown);
}
