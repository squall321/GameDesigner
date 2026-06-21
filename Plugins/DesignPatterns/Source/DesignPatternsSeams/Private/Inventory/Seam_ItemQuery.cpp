// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Inventory/Seam_ItemQuery.h"

// Default implementations: a provider that does not override these reports an empty inventory, so an
// absent/partial implementer fails closed (no items) rather than asserting. Real implementers (the RPG
// inventory component) override both.

int32 ISeam_ItemQuery::GetItemCount_Implementation(FGameplayTag /*ItemTag*/) const
{
	return 0;
}

bool ISeam_ItemQuery::HasItem_Implementation(FGameplayTag /*ItemTag*/, int32 Count) const
{
	return Count <= 0;
}
