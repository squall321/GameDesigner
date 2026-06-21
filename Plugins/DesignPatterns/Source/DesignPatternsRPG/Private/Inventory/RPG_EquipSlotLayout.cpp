// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Inventory/RPG_EquipSlotLayout.h"

const FRPG_EquipSlotDef* URPG_EquipSlotLayout::FindSlot(const FGameplayTag& SlotTag) const
{
	return Slots.FindByPredicate([&SlotTag](const FRPG_EquipSlotDef& Slot)
	{
		return Slot.SlotTag == SlotTag;
	});
}

bool URPG_EquipSlotLayout::AcceptsType(FGameplayTag SlotTag, FGameplayTag ItemTypeTag) const
{
	const FRPG_EquipSlotDef* Slot = FindSlot(SlotTag);
	if (!Slot)
	{
		return false;
	}
	if (Slot->AcceptedItemTypes.IsEmpty())
	{
		return true; // unrestricted slot
	}
	return ItemTypeTag.IsValid() && Slot->AcceptedItemTypes.HasTag(ItemTypeTag);
}

FName URPG_EquipSlotLayout::GetDataAssetType_Implementation() const
{
	return FName(TEXT("RPG_EquipSlotLayout"));
}
