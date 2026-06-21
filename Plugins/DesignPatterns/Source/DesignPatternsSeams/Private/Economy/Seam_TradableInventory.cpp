// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Economy/Seam_TradableInventory.h"

// Default implementations: an unoverridden provider can remove nothing, so escrow against a non-implementer
// fails closed (no item leaves the inventory) rather than asserting. The RPG inventory adapter overrides.

bool ISeam_TradableInventory::CanRemove_Implementation(FGameplayTag /*ItemTag*/, int32 /*Count*/) const
{
	return false;
}

int32 ISeam_TradableInventory::RemoveItem_Implementation(FGameplayTag /*ItemTag*/, int32 /*Count*/)
{
	return 0;
}
