// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Journal/RPG_LoreDataAsset.h"

FName URPG_LoreDataAsset::GetDataAssetType_Implementation() const
{
	return FName(TEXT("RPG_Lore"));
}

const FRPG_LoreEntry* URPG_LoreDataAsset::FindEntry(FGameplayTag LoreTag) const
{
	if (!LoreTag.IsValid())
	{
		return nullptr;
	}
	for (const FRPG_LoreEntry& Entry : Entries)
	{
		if (Entry.LoreTag == LoreTag)
		{
			return &Entry;
		}
	}
	return nullptr;
}
