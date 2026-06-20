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
