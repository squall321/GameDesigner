// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Seam/InvUI_ItemDisplay.h"

// Default (unoverridden) implementations. A backend that does not implement the item display seam
// returns unresolved and returns false on cache lookup, so the UI falls back to tag-derived display.

void IInvUI_ItemDisplay::ResolveItemDisplay_Implementation(FGameplayTag ItemTag, const FInvUI_OnItemDisplayResolved& OnResolved)
{
	// Fire the delegate immediately with the unresolved flag so the caller can clear pending state.
	FInvUI_ItemDisplayInfo UnresolvedInfo;
	UnresolvedInfo.ItemTag = ItemTag;
	UnresolvedInfo.bResolved = false;
	OnResolved.ExecuteIfBound(UnresolvedInfo);
}

bool IInvUI_ItemDisplay::TryGetCachedDisplay_Implementation(FGameplayTag /*ItemTag*/, FInvUI_ItemDisplayInfo& OutInfo) const
{
	OutInfo = FInvUI_ItemDisplayInfo();
	return false; // No cached info available by default
}
