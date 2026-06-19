// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Item/RPG_ItemDefinition.h"

URPG_ItemDefinition::URPG_ItemDefinition()
{
	MaxStackSize = 1;
}

FName URPG_ItemDefinition::GetDataAssetType_Implementation() const
{
	// All item definitions share one primary asset type so the asset manager can address
	// the whole item catalog as a single bucket regardless of concrete subclass.
	return FName(TEXT("RPG_Item"));
}
