// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Inventory/RPG_ItemInstanceComponent.h"
#include "Item/RPG_DepthTags.h"
#include "Core/DPLog.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

//~ FRPG_ItemInstanceEntry replication callbacks (client side) ---------------------------------

void FRPG_ItemInstanceEntry::PreReplicatedRemove(const FRPG_ItemInstanceArray& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->HandleReplicatedChange();
	}
}

void FRPG_ItemInstanceEntry::PostReplicatedAdd(const FRPG_ItemInstanceArray& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->HandleReplicatedChange();
	}
}

void FRPG_ItemInstanceEntry::PostReplicatedChange(const FRPG_ItemInstanceArray& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->HandleReplicatedChange();
	}
}

//~ URPG_ItemInstanceComponent ----------------------------------------------------------------

URPG_ItemInstanceComponent::URPG_ItemInstanceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);

	// Wire the fast-array back-pointer so entry callbacks can notify us (server and client).
	Instances.OwnerComponent = this;
}

void URPG_ItemInstanceComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(URPG_ItemInstanceComponent, Instances);
}

int32 URPG_ItemInstanceComponent::FindEntryIndex(int32 InstanceId) const
{
	for (int32 Index = 0; Index < Instances.Entries.Num(); ++Index)
	{
		if (Instances.Entries[Index].Instance.InstanceId == InstanceId)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

int32 URPG_ItemInstanceComponent::AddInstance(const FRPG_ItemInstance& Instance)
{
	// AUTHORITY GUARD: never mutate replicated instance state on a client.
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return 0;
	}
	if (!Instance.ItemTag.IsValid())
	{
		return 0;
	}

	FRPG_ItemInstanceEntry& NewEntry = Instances.Entries.AddDefaulted_GetRef();
	NewEntry.Instance = Instance;
	NewEntry.Instance.InstanceId = NextInstanceId++; // assign a fresh stable id, overwriting any inbound id
	Instances.MarkItemDirty(NewEntry);

	NotifyInstancesChanged();
	UE_LOG(LogDPData, Verbose, TEXT("[RPG_ItemInstance] Added instance %d (%s)"),
		NewEntry.Instance.InstanceId, *NewEntry.Instance.ItemTag.ToString());
	return NewEntry.Instance.InstanceId;
}

bool URPG_ItemInstanceComponent::RemoveInstance(int32 InstanceId)
{
	// AUTHORITY GUARD.
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}
	const int32 Index = FindEntryIndex(InstanceId);
	if (Index == INDEX_NONE)
	{
		return false;
	}

	Instances.Entries.RemoveAt(Index);
	Instances.MarkArrayDirty();

	NotifyInstancesChanged();
	UE_LOG(LogDPData, Verbose, TEXT("[RPG_ItemInstance] Removed instance %d"), InstanceId);
	return true;
}

bool URPG_ItemInstanceComponent::GetInstance(int32 InstanceId, FRPG_ItemInstance& Out) const
{
	const int32 Index = FindEntryIndex(InstanceId);
	if (Index == INDEX_NONE)
	{
		return false;
	}
	Out = Instances.Entries[Index].Instance;
	return true;
}

TArray<FRPG_ItemInstance> URPG_ItemInstanceComponent::GetAllInstances() const
{
	TArray<FRPG_ItemInstance> Result;
	Result.Reserve(Instances.Entries.Num());
	for (const FRPG_ItemInstanceEntry& Entry : Instances.Entries)
	{
		Result.Add(Entry.Instance);
	}
	return Result;
}

bool URPG_ItemInstanceComponent::HasInstance(int32 InstanceId) const
{
	return FindEntryIndex(InstanceId) != INDEX_NONE;
}

bool URPG_ItemInstanceComponent::MutateInstance(int32 InstanceId, TFunctionRef<void(FRPG_ItemInstance&)> Mutator)
{
	// AUTHORITY GUARD.
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}
	const int32 Index = FindEntryIndex(InstanceId);
	if (Index == INDEX_NONE)
	{
		return false;
	}

	FRPG_ItemInstanceEntry& Entry = Instances.Entries[Index];
	Mutator(Entry.Instance);
	Entry.Instance.InstanceId = InstanceId; // guard against a mutator clobbering the stable id
	Instances.MarkItemDirty(Entry);

	NotifyInstancesChanged();
	return true;
}

void URPG_ItemInstanceComponent::NotifyInstancesChanged()
{
	OnInstancesChanged.Broadcast(this);
}

void URPG_ItemInstanceComponent::HandleReplicatedChange()
{
	// Reached on clients from the fast-array entry callbacks: surface the change so equip/encumbrance
	// recompute can re-resolve instance data regardless of replication order.
	NotifyInstancesChanged();
}

//~ ISeam_Persistable -------------------------------------------------------------------------

void URPG_ItemInstanceComponent::CaptureState_Implementation(FInstancedStruct& Out) const
{
	FRPG_ItemInstanceSaveData Record;
	Record.NextInstanceId = NextInstanceId;
	Record.Instances.Reserve(Instances.Entries.Num());
	for (const FRPG_ItemInstanceEntry& Entry : Instances.Entries)
	{
		Record.Instances.Add(Entry.Instance);
	}
	Out.InitializeAs<FRPG_ItemInstanceSaveData>(Record);
}

void URPG_ItemInstanceComponent::RestoreState_Implementation(const FInstancedStruct& In)
{
	// AUTHORITY GUARD: a client-side load must be a no-op (state arrives via replication).
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	const FRPG_ItemInstanceSaveData* Record = In.GetPtr<FRPG_ItemInstanceSaveData>();
	if (!Record)
	{
		UE_LOG(LogDPSave, Warning,
			TEXT("[RPG_ItemInstance] RestoreState got a payload that is not FRPG_ItemInstanceSaveData on '%s'."),
			*GetNameSafe(GetOwner()));
		return;
	}

	Instances.Entries.Reset(Record->Instances.Num());
	for (const FRPG_ItemInstance& Saved : Record->Instances)
	{
		FRPG_ItemInstanceEntry& Entry = Instances.Entries.AddDefaulted_GetRef();
		Entry.Instance = Saved;
	}
	Instances.MarkArrayDirty();

	// Keep the id source ahead of every restored id so future adds never collide with a loaded instance.
	int32 MaxId = Record->NextInstanceId;
	for (const FRPG_ItemInstance& Saved : Record->Instances)
	{
		MaxId = FMath::Max(MaxId, Saved.InstanceId + 1);
	}
	NextInstanceId = FMath::Max(1, MaxId);

	NotifyInstancesChanged();
}

FGameplayTag URPG_ItemInstanceComponent::GetPersistenceKind_Implementation() const
{
	return RPG_DepthTags::Persist_ItemInstances;
}
