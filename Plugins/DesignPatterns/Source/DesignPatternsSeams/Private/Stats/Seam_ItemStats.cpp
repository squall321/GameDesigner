// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Stats/Seam_ItemStats.h"

// Default (unoverridden) implementations. When no real item-stats resolver is present, the
// synchronous fast path reports "not cached" (false) and the async path immediately reports an
// unresolved set so a comparison view clears its pending state instead of hanging. A project
// without an item-stats module still runs deterministically; the real implementer (the RPG/items
// adapter published under the project's item-stats service key) overrides both methods.

bool ISeam_ItemStats::TryGetItemStats_Implementation(FGameplayTag /*ItemTag*/, FSeam_ItemStatSet& Out) const
{
	Out = FSeam_ItemStatSet();
	return false; // nothing cached -> caller falls back to the async path
}

void ISeam_ItemStats::ResolveItemStats_Implementation(FGameplayTag ItemTag, const FSeam_OnItemStatsResolved& OnResolved)
{
	// Fire once with an unresolved set so callers can clear pending state (mirrors the
	// IInvUI_ItemDisplay contract: the delegate is always invoked exactly once).
	FSeam_ItemStatSet Unresolved;
	Unresolved.ItemTag = ItemTag;
	Unresolved.bResolved = false;
	OnResolved.ExecuteIfBound(Unresolved);
}
