// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Interfaces/SimGrid_Placeable.h"

/**
 * Default BlueprintNativeEvent bodies for ISimGrid_Placeable. Concrete placeables (building actors,
 * furniture, turrets) override these. The defaults are inert/fail-safe: an unimplemented placeable
 * reports an empty footprint and no terrain requirements, and its authority-only lifecycle hooks are
 * no-ops. This lets the placement system (SimGrid_PlacementComponent's Execute_* calls) link and run
 * even when a Blueprint actor only partially implements the interface, with no cosmetic or state effect.
 */

TArray<FSimGrid_FootprintCell> ISimGrid_Placeable::GetFootprint_Implementation() const
{
	return TArray<FSimGrid_FootprintCell>();
}

FGameplayTagContainer ISimGrid_Placeable::GetRequiredTileTypes_Implementation() const
{
	return FGameplayTagContainer();
}

void ISimGrid_Placeable::OnPlaced_Implementation(const FSeam_CellCoord& /*Origin*/, ESimGrid_Rotation /*Rotation*/)
{
}

void ISimGrid_Placeable::OnRemoved_Implementation()
{
}
