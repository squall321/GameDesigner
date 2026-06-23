// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Seam/InvUI_SpatialFootprintProvider.h"

// Default (unoverridden) implementations. A backend that does not implement the spatial provider
// reports "no footprint" and "no explicit anchor", so the spatial layout treats its slots as 1x1
// cells auto-packed with first-fit. Every shipped (non-spatial) backend therefore compiles and
// lays out unchanged.

bool IInvUI_SpatialFootprintProvider::GetSlotFootprint_Implementation(FGameplayTag /*SlotTag*/, FInvUI_SpatialFootprint& Out) const
{
	Out = FInvUI_SpatialFootprint();
	return false;
}

bool IInvUI_SpatialFootprintProvider::GetSlotAnchorCell_Implementation(FGameplayTag /*SlotTag*/, FIntPoint& OutCell) const
{
	OutCell = FIntPoint::ZeroValue;
	return false;
}
