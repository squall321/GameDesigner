// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Inventory/RPG_InventoryComponent.h"
#include "Item/RPG_ItemDefinition.h"
#include "Data/DPDataRegistrySubsystem.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

//~ FRPG_InventoryEntry replication callbacks (client side) -----------------------------------

void FRPG_InventoryEntry::PreReplicatedRemove(const FRPG_InventoryArray& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->HandleReplicatedChange();
	}
}

void FRPG_InventoryEntry::PostReplicatedAdd(const FRPG_InventoryArray& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->HandleReplicatedChange();
	}
}

void FRPG_InventoryEntry::PostReplicatedChange(const FRPG_InventoryArray& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->HandleReplicatedChange();
	}
}

//~ URPG_InventoryComponent -------------------------------------------------------------------

URPG_InventoryComponent::URPG_InventoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);

	// Wire the fast-array back-pointer so entry callbacks can notify us (server and client).
	Inventory.OwnerComponent = this;
}

void URPG_InventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(URPG_InventoryComponent, Inventory);
}

int32 URPG_InventoryComponent::ResolveMaxStack(const FGameplayTag& ItemTag) const
{
	if (!ItemTag.IsValid())
	{
		return 1;
	}
	if (UDP_DataRegistrySubsystem* Registry =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_DataRegistrySubsystem>(this))
	{
		if (const URPG_ItemDefinition* Def = Registry->Find<URPG_ItemDefinition>(ItemTag))
		{
			return FMath::Max(1, Def->MaxStackSize);
		}
	}
	return 1;
}

void URPG_InventoryComponent::MarkEntryDirtyAndNotify(FRPG_InventoryEntry& Entry)
{
	Inventory.MarkItemDirty(Entry);
	NotifyInventoryChanged();
}

int32 URPG_InventoryComponent::AddItem(FGameplayTag ItemTag, int32 Count)
{
	// AUTHORITY GUARD: never mutate replicated inventory state on a client.
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return 0;
	}
	if (!ItemTag.IsValid() || Count <= 0)
	{
		return 0;
	}

	const int32 MaxStack = ResolveMaxStack(ItemTag);
	int32 Remaining = Count;

	// First top up existing stacks of this item that have headroom.
	for (FRPG_InventoryEntry& Entry : Inventory.Entries)
	{
		if (Remaining <= 0)
		{
			break;
		}
		if (Entry.Stack.ItemTag == ItemTag && Entry.Stack.Count < MaxStack)
		{
			const int32 Headroom = MaxStack - Entry.Stack.Count;
			const int32 ToAdd = FMath::Min(Headroom, Remaining);
			Entry.Stack.Count += ToAdd;
			Remaining -= ToAdd;
			Inventory.MarkItemDirty(Entry);
		}
	}

	// Overflow into new stacks.
	while (Remaining > 0)
	{
		const int32 ToAdd = FMath::Min(MaxStack, Remaining);
		FRPG_InventoryEntry& NewEntry = Inventory.Entries.AddDefaulted_GetRef();
		NewEntry.Stack = FRPG_ItemStack(ItemTag, ToAdd);
		Remaining -= ToAdd;
		Inventory.MarkItemDirty(NewEntry);
	}

	NotifyInventoryChanged();
	UE_LOG(LogDPData, Verbose, TEXT("[RPG_Inventory] Added %d x %s"), Count, *ItemTag.ToString());
	return Count;
}

int32 URPG_InventoryComponent::RemoveItem_Implementation(FGameplayTag ItemTag, int32 Count)
{
	// AUTHORITY GUARD.
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return 0;
	}
	if (!ItemTag.IsValid() || Count <= 0)
	{
		return 0;
	}

	int32 Remaining = Count;
	bool bChanged = false;

	// Drain from the last stacks first so the array shrinks cleanly.
	for (int32 Index = Inventory.Entries.Num() - 1; Index >= 0 && Remaining > 0; --Index)
	{
		FRPG_InventoryEntry& Entry = Inventory.Entries[Index];
		if (Entry.Stack.ItemTag != ItemTag)
		{
			continue;
		}
		const int32 ToRemove = FMath::Min(Entry.Stack.Count, Remaining);
		Entry.Stack.Count -= ToRemove;
		Remaining -= ToRemove;
		bChanged = true;

		if (Entry.Stack.Count <= 0)
		{
			Inventory.Entries.RemoveAt(Index);
			Inventory.MarkArrayDirty();
		}
		else
		{
			Inventory.MarkItemDirty(Entry);
		}
	}

	const int32 Removed = Count - Remaining;
	if (bChanged)
	{
		NotifyInventoryChanged();
		UE_LOG(LogDPData, Verbose, TEXT("[RPG_Inventory] Removed %d x %s"), Removed, *ItemTag.ToString());
	}
	return Removed;
}

int32 URPG_InventoryComponent::MoveItem(URPG_InventoryComponent* Target, FGameplayTag ItemTag, int32 Count)
{
	// AUTHORITY GUARD on THIS inventory.
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return 0;
	}
	if (!Target || Target == this || !ItemTag.IsValid() || Count <= 0)
	{
		return 0;
	}

	// Only remove what we actually hold, then add exactly that to the target.
	const int32 Removed = RemoveItem_Implementation(ItemTag, Count);
	if (Removed > 0)
	{
		Target->AddItem(ItemTag, Removed);
	}
	return Removed;
}

int32 URPG_InventoryComponent::GetItemCount_Implementation(FGameplayTag ItemTag) const
{
	int32 Total = 0;
	for (const FRPG_InventoryEntry& Entry : Inventory.Entries)
	{
		if (Entry.Stack.ItemTag == ItemTag)
		{
			Total += Entry.Stack.Count;
		}
	}
	return Total;
}

bool URPG_InventoryComponent::HasItem_Implementation(FGameplayTag ItemTag, int32 Count) const
{
	return GetItemCount_Implementation(ItemTag) >= Count;
}

bool URPG_InventoryComponent::CanRemove_Implementation(FGameplayTag ItemTag, int32 Count) const
{
	// Read-only pre-check for trade/escrow flows: do we currently hold enough to remove Count?
	return Count > 0 && GetItemCount_Implementation(ItemTag) >= Count;
}

TArray<FRPG_ItemStack> URPG_InventoryComponent::GetAllStacks() const
{
	TArray<FRPG_ItemStack> Result;
	Result.Reserve(Inventory.Entries.Num());
	for (const FRPG_InventoryEntry& Entry : Inventory.Entries)
	{
		Result.Add(Entry.Stack);
	}
	return Result;
}

float URPG_InventoryComponent::GetTotalWeight() const
{
	float Total = 0.f;
	UDP_DataRegistrySubsystem* Registry =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_DataRegistrySubsystem>(this);
	for (const FRPG_InventoryEntry& Entry : Inventory.Entries)
	{
		float UnitWeight = 0.f;
		if (Registry)
		{
			if (const URPG_ItemDefinition* Def = Registry->Find<URPG_ItemDefinition>(Entry.Stack.ItemTag))
			{
				UnitWeight = Def->Weight;
			}
		}
		Total += UnitWeight * static_cast<float>(Entry.Stack.Count);
	}
	return Total;
}

void URPG_InventoryComponent::HandleReplicatedChange()
{
	// Reached on clients from the fast-array entry callbacks: surface the change.
	NotifyInventoryChanged();
}

void URPG_InventoryComponent::NotifyInventoryChanged_Implementation()
{
	OnInventoryChanged.Broadcast(this);
}
