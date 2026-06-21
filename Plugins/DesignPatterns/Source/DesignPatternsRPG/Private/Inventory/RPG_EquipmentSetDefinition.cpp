// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Inventory/RPG_EquipmentSetDefinition.h"

int32 URPG_EquipmentSetDefinition::CountWornMembers(const TArray<FGameplayTag>& EquippedItemTags) const
{
	int32 Count = 0;
	for (const FGameplayTag& ItemTag : EquippedItemTags)
	{
		if (ItemTag.IsValid() && MemberItems.HasTag(ItemTag))
		{
			++Count;
		}
	}
	return Count;
}

void URPG_EquipmentSetDefinition::GatherActiveBonuses(int32 WornCount, const FGameplayTag& SourceKey,
	TArray<FRPG_StatModifier>& Out) const
{
	for (const FRPG_SetBonusTier& Tier : BonusTiers)
	{
		if (WornCount >= Tier.RequiredPieces)
		{
			for (const FRPG_StatModifier& Mod : Tier.Modifiers)
			{
				FRPG_StatModifier Copy = Mod;
				Copy.SourceTag = SourceKey; // unify the whole set's contribution under one removable group
				Out.Add(Copy);
			}
		}
	}
}

FName URPG_EquipmentSetDefinition::GetDataAssetType_Implementation() const
{
	return FName(TEXT("RPG_EquipmentSet"));
}
