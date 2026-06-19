// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Inventory/RPG_EquipmentComponent.h"
#include "Core/DPLog.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

//~ FRPG_EquipmentEntry replication callbacks (client side) -----------------------------------

void FRPG_EquipmentEntry::PreReplicatedRemove(const FRPG_EquipmentArray& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->HandleReplicatedChange(SlotTag);
	}
}

void FRPG_EquipmentEntry::PostReplicatedAdd(const FRPG_EquipmentArray& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->HandleReplicatedChange(SlotTag);
	}
}

void FRPG_EquipmentEntry::PostReplicatedChange(const FRPG_EquipmentArray& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->HandleReplicatedChange(SlotTag);
	}
}

//~ URPG_EquipmentComponent -------------------------------------------------------------------

URPG_EquipmentComponent::URPG_EquipmentComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);

	Equipment.OwnerComponent = this;
}

void URPG_EquipmentComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(URPG_EquipmentComponent, Equipment);
}

int32 URPG_EquipmentComponent::FindSlotIndex(const FGameplayTag& SlotTag) const
{
	for (int32 Index = 0; Index < Equipment.Entries.Num(); ++Index)
	{
		if (Equipment.Entries[Index].SlotTag == SlotTag)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

bool URPG_EquipmentComponent::Equip(FGameplayTag SlotTag, FGameplayTag ItemTag)
{
	// AUTHORITY GUARD: never mutate replicated equipment state on a client.
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}
	if (!SlotTag.IsValid() || !ItemTag.IsValid())
	{
		return false;
	}

	const int32 Index = FindSlotIndex(SlotTag);
	if (Index != INDEX_NONE)
	{
		FRPG_EquipmentEntry& Entry = Equipment.Entries[Index];
		if (Entry.ItemTag == ItemTag)
		{
			return false; // no change
		}
		Entry.ItemTag = ItemTag;
		Equipment.MarkItemDirty(Entry);
	}
	else
	{
		FRPG_EquipmentEntry& NewEntry = Equipment.Entries.AddDefaulted_GetRef();
		NewEntry.SlotTag = SlotTag;
		NewEntry.ItemTag = ItemTag;
		Equipment.MarkItemDirty(NewEntry);
	}

	NotifyEquipmentChanged(SlotTag);
	UE_LOG(LogDPData, Verbose, TEXT("[RPG_Equipment] Equipped %s into %s"),
		*ItemTag.ToString(), *SlotTag.ToString());
	return true;
}

bool URPG_EquipmentComponent::Unequip(FGameplayTag SlotTag)
{
	// AUTHORITY GUARD.
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}

	const int32 Index = FindSlotIndex(SlotTag);
	if (Index == INDEX_NONE)
	{
		return false;
	}

	Equipment.Entries.RemoveAt(Index);
	Equipment.MarkArrayDirty();

	NotifyEquipmentChanged(SlotTag);
	UE_LOG(LogDPData, Verbose, TEXT("[RPG_Equipment] Unequipped slot %s"), *SlotTag.ToString());
	return true;
}

FGameplayTag URPG_EquipmentComponent::GetEquippedItem(FGameplayTag SlotTag) const
{
	const int32 Index = FindSlotIndex(SlotTag);
	return Index != INDEX_NONE ? Equipment.Entries[Index].ItemTag : FGameplayTag();
}

TArray<FGameplayTag> URPG_EquipmentComponent::GetOccupiedSlots() const
{
	TArray<FGameplayTag> Result;
	Result.Reserve(Equipment.Entries.Num());
	for (const FRPG_EquipmentEntry& Entry : Equipment.Entries)
	{
		Result.Add(Entry.SlotTag);
	}
	return Result;
}

void URPG_EquipmentComponent::HandleReplicatedChange(FGameplayTag SlotTag)
{
	NotifyEquipmentChanged(SlotTag);
}

void URPG_EquipmentComponent::NotifyEquipmentChanged_Implementation(FGameplayTag SlotTag)
{
	OnEquipmentChanged.Broadcast(this, SlotTag);
}
