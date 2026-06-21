// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Inventory/RPG_EquipSlotsComponent.h"
#include "Inventory/RPG_EquipSlotLayout.h"
#include "Inventory/RPG_EquipmentSetDefinition.h"
#include "Inventory/RPG_ItemInstanceComponent.h"
#include "Inventory/RPG_InventoryComponent.h"
#include "Item/RPG_ItemDefinition.h"
#include "Item/RPG_ItemInstance.h"
#include "Item/RPG_AffixDefinition.h"
#include "Item/RPG_DepthTags.h"
#include "Stats/Seam_StatModifierSink.h"
#include "Stats/Seam_StatMod.h"
#include "Data/DPDataRegistrySubsystem.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

//~ FRPG_EquipSlotInstanceEntry replication callbacks (client side) ----------------------------

void FRPG_EquipSlotInstanceEntry::PreReplicatedRemove(const FRPG_EquipSlotInstanceArray& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->RecomputeSlotModifiers(SlotTag);
	}
}

void FRPG_EquipSlotInstanceEntry::PostReplicatedAdd(const FRPG_EquipSlotInstanceArray& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->RecomputeSlotModifiers(SlotTag);
	}
}

void FRPG_EquipSlotInstanceEntry::PostReplicatedChange(const FRPG_EquipSlotInstanceArray& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->RecomputeSlotModifiers(SlotTag);
	}
}

//~ URPG_EquipSlotsComponent ------------------------------------------------------------------

URPG_EquipSlotsComponent::URPG_EquipSlotsComponent()
{
	SlotInstanceBindings.OwnerComponent = this;
}

void URPG_EquipSlotsComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(URPG_EquipSlotsComponent, SlotInstanceBindings);
}

void URPG_EquipSlotsComponent::BeginPlay()
{
	Super::BeginPlay();

	// Bind the sibling instance carrier so a late-arriving instance re-triggers our recompute (server+client).
	if (URPG_ItemInstanceComponent* InstanceComp = ResolveInstanceComponent())
	{
		if (!bBoundInstanceDelegate)
		{
			InstanceComp->OnInstancesChanged.AddDynamic(this, &URPG_EquipSlotsComponent::HandleInstancesChanged);
			bBoundInstanceDelegate = true;
		}
	}

	// Initial derivation from whatever state already replicated.
	for (const FGameplayTag& SlotTag : GetOccupiedSlots())
	{
		RecomputeSlotModifiers(SlotTag);
	}
	RecomputeSetBonuses();
}

void URPG_EquipSlotsComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bBoundInstanceDelegate)
	{
		if (URPG_ItemInstanceComponent* InstanceComp = ResolveInstanceComponent())
		{
			InstanceComp->OnInstancesChanged.RemoveDynamic(this, &URPG_EquipSlotsComponent::HandleInstancesChanged);
		}
		bBoundInstanceDelegate = false;
	}
	Super::EndPlay(EndPlayReason);
}

int32 URPG_EquipSlotsComponent::FindBindingIndex(const FGameplayTag& SlotTag) const
{
	for (int32 Index = 0; Index < SlotInstanceBindings.Entries.Num(); ++Index)
	{
		if (SlotInstanceBindings.Entries[Index].SlotTag == SlotTag)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

void URPG_EquipSlotsComponent::SetBinding(const FGameplayTag& SlotTag, int32 InstanceId)
{
	// AUTHORITY-side helper; callers already guard.
	const int32 Index = FindBindingIndex(SlotTag);
	if (InstanceId == 0)
	{
		if (Index != INDEX_NONE)
		{
			SlotInstanceBindings.Entries.RemoveAt(Index);
			SlotInstanceBindings.MarkArrayDirty();
		}
		return;
	}

	if (Index != INDEX_NONE)
	{
		FRPG_EquipSlotInstanceEntry& Entry = SlotInstanceBindings.Entries[Index];
		Entry.InstanceId = InstanceId;
		SlotInstanceBindings.MarkItemDirty(Entry);
	}
	else
	{
		FRPG_EquipSlotInstanceEntry& NewEntry = SlotInstanceBindings.Entries.AddDefaulted_GetRef();
		NewEntry.SlotTag = SlotTag;
		NewEntry.InstanceId = InstanceId;
		SlotInstanceBindings.MarkItemDirty(NewEntry);
	}
}

TScriptInterface<ISeam_StatModifierSink> URPG_EquipSlotsComponent::ResolveStatSink() const
{
	if (CachedStatSink.GetObject())
	{
		return CachedStatSink;
	}
	TScriptInterface<ISeam_StatModifierSink> Result;
	if (const AActor* Owner = GetOwner())
	{
		TArray<UActorComponent*> Components;
		Owner->GetComponents(Components);
		for (UActorComponent* Comp : Components)
		{
			if (Comp && Comp->GetClass()->ImplementsInterface(USeam_StatModifierSink::StaticClass()))
			{
				Result.SetObject(Comp);
				Result.SetInterface(Cast<ISeam_StatModifierSink>(Comp));
				break;
			}
		}
	}
	return Result;
}

URPG_ItemInstanceComponent* URPG_EquipSlotsComponent::ResolveInstanceComponent() const
{
	if (const AActor* Owner = GetOwner())
	{
		return Owner->FindComponentByClass<URPG_ItemInstanceComponent>();
	}
	return nullptr;
}

FGameplayTag URPG_EquipSlotsComponent::MakeSlotSourceTag(const FGameplayTag& SlotTag) const
{
	// The sink groups modifiers by SourceTag and replaces a whole group atomically. The slot tag is a stable,
	// per-slot-unique key, so it is used directly as the source key for that slot's derived modifier group
	// (one group per slot). When called with an invalid slot, fall back to the shared equip source root.
	return SlotTag.IsValid() ? SlotTag : RPG_DepthTags::StatSource_Equip;
}

bool URPG_EquipSlotsComponent::EquipInstance(FGameplayTag SlotTag, int32 InstanceId)
{
	// AUTHORITY GUARD: equipment + bindings are replicated, server-authoritative state.
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}
	if (!SlotTag.IsValid() || InstanceId == 0)
	{
		return false;
	}

	URPG_ItemInstanceComponent* InstanceComp = ResolveInstanceComponent();
	if (!InstanceComp)
	{
		UE_LOG(LogDPData, Warning, TEXT("[RPG_EquipSlots] EquipInstance: no instance component on '%s'."),
			*GetNameSafe(GetOwner()));
		return false;
	}

	FRPG_ItemInstance Instance;
	if (!InstanceComp->GetInstance(InstanceId, Instance))
	{
		return false;
	}

	// Validate the item type against the slot layout, when one is authored.
	if (SlotLayout)
	{
		FGameplayTag ItemTypeTag;
		if (UDP_DataRegistrySubsystem* Registry =
			FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_DataRegistrySubsystem>(this))
		{
			if (const URPG_ItemDefinition* Def = Registry->Find<URPG_ItemDefinition>(Instance.ItemTag))
			{
				ItemTypeTag = Def->ItemTypeTag;
			}
		}
		if (!SlotLayout->AcceptsType(SlotTag, ItemTypeTag))
		{
			UE_LOG(LogDPData, Verbose,
				TEXT("[RPG_EquipSlots] EquipInstance rejected: slot %s does not accept type %s."),
				*SlotTag.ToString(), *ItemTypeTag.ToString());
			return false;
		}
	}

	// Base equip by item tag (fires NotifyEquipmentChanged -> RecomputeSlotModifiers), then record the id.
	SetBinding(SlotTag, InstanceId);
	Equip(SlotTag, Instance.ItemTag);
	// If the item tag was unchanged the base Equip returns false WITHOUT notifying, so force a recompute.
	RecomputeSlotModifiers(SlotTag);
	RecomputeSetBonuses();
	return true;
}

bool URPG_EquipSlotsComponent::UnequipSlot(FGameplayTag SlotTag)
{
	// AUTHORITY GUARD.
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}
	SetBinding(SlotTag, 0);
	const bool bWasOccupied = Unequip(SlotTag); // fires NotifyEquipmentChanged -> recompute clears the group
	RecomputeSlotModifiers(SlotTag);
	RecomputeSetBonuses();
	return bWasOccupied;
}

int32 URPG_EquipSlotsComponent::GetEquippedInstanceId(FGameplayTag SlotTag) const
{
	const int32 Index = FindBindingIndex(SlotTag);
	return Index != INDEX_NONE ? SlotInstanceBindings.Entries[Index].InstanceId : 0;
}

bool URPG_EquipSlotsComponent::SocketGem(FGameplayTag SlotTag, int32 SocketIndex, FGameplayTag GemItemTag)
{
	// AUTHORITY GUARD.
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}
	if (!GemItemTag.IsValid())
	{
		return false;
	}
	URPG_ItemInstanceComponent* InstanceComp = ResolveInstanceComponent();
	const int32 InstanceId = GetEquippedInstanceId(SlotTag);
	if (!InstanceComp || InstanceId == 0)
	{
		return false;
	}

	bool bApplied = false;
	InstanceComp->MutateInstance(InstanceId, [&](FRPG_ItemInstance& Inst)
	{
		if (Inst.Sockets.IsValidIndex(SocketIndex) && Inst.Sockets[SocketIndex].IsOpen())
		{
			Inst.Sockets[SocketIndex].SocketedItemTag = GemItemTag;
			bApplied = true;
		}
	});

	if (bApplied)
	{
		RecomputeSlotModifiers(SlotTag);
	}
	return bApplied;
}

bool URPG_EquipSlotsComponent::UnsocketGem(FGameplayTag SlotTag, int32 SocketIndex, bool bReturnToInventory)
{
	// AUTHORITY GUARD.
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}
	URPG_ItemInstanceComponent* InstanceComp = ResolveInstanceComponent();
	const int32 InstanceId = GetEquippedInstanceId(SlotTag);
	if (!InstanceComp || InstanceId == 0)
	{
		return false;
	}

	FGameplayTag RemovedGem;
	bool bRemoved = false;
	InstanceComp->MutateInstance(InstanceId, [&](FRPG_ItemInstance& Inst)
	{
		if (Inst.Sockets.IsValidIndex(SocketIndex) && !Inst.Sockets[SocketIndex].IsOpen())
		{
			RemovedGem = Inst.Sockets[SocketIndex].SocketedItemTag;
			Inst.Sockets[SocketIndex].SocketedItemTag = FGameplayTag();
			bRemoved = true;
		}
	});

	if (bRemoved)
	{
		if (bReturnToInventory && RemovedGem.IsValid())
		{
			if (URPG_InventoryComponent* Inventory = GetOwner()->FindComponentByClass<URPG_InventoryComponent>())
			{
				Inventory->AddItem(RemovedGem, 1);
			}
		}
		RecomputeSlotModifiers(SlotTag);
	}
	return bRemoved;
}

void URPG_EquipSlotsComponent::NotifyEquipmentChanged_Implementation(FGameplayTag SlotTag)
{
	// Super FIRST so the base OnEquipmentChanged still fires for UI, then derive stat modifiers.
	Super::NotifyEquipmentChanged_Implementation(SlotTag);
	RecomputeSlotModifiers(SlotTag);
	RecomputeSetBonuses();
}

void URPG_EquipSlotsComponent::HandleInstancesChanged(URPG_ItemInstanceComponent* /*Component*/)
{
	// An instance's affixes/sockets changed (or a late instance arrived): re-derive every occupied slot.
	for (const FGameplayTag& SlotTag : GetOccupiedSlots())
	{
		RecomputeSlotModifiers(SlotTag);
	}
	RecomputeSetBonuses();
}

void URPG_EquipSlotsComponent::RecomputeSlotModifiers(FGameplayTag SlotTag)
{
	// NO AUTHORITY GUARD: pure local derivation from replicated state. Runs on server AND clients.
	if (!SlotTag.IsValid())
	{
		return;
	}

	TScriptInterface<ISeam_StatModifierSink> Sink = ResolveStatSink();
	if (!Sink.GetObject())
	{
		return; // no stats component on this actor; nothing to contribute to
	}
	// Refresh the cache for subsequent recomputes.
	CachedStatSink = Sink;

	const FGameplayTag SourceTag = MakeSlotSourceTag(SlotTag);

	TArray<FSeam_StatMod> Mods;
	const int32 InstanceId = GetEquippedInstanceId(SlotTag);
	if (InstanceId != 0)
	{
		if (URPG_ItemInstanceComponent* InstanceComp = ResolveInstanceComponent())
		{
			FRPG_ItemInstance Instance;
			if (InstanceComp->GetInstance(InstanceId, Instance))
			{
				// Affixes -> modifiers.
				for (const FRPG_ItemAffix& Affix : Instance.Affixes)
				{
					if (!Affix.IsValidAffix())
					{
						continue;
					}
					Mods.Add(FSeam_StatMod(
						Affix.AttributeTag,
						static_cast<uint8>(Affix.Op),
						static_cast<double>(Affix.GetMagnitude()),
						SourceTag));
				}

				// Socketed gems -> modifiers. A gem is itself an item definition; its stat contribution is
				// authored as a URPG_EquipmentSetDefinition-style bonus is out of scope, so a gem contributes
				// through gem-affix data carried by a dedicated affix definition resolved by the gem's tag.
				// We resolve each socketed gem's affix definition (DataTag == gem item tag) and, when present,
				// roll it deterministically at the host item's level using the gem tag as a stable seed so the
				// same gem always grants the same modifier on every machine (pure local derivation).
				if (UDP_DataRegistrySubsystem* Registry =
					FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_DataRegistrySubsystem>(this))
				{
					for (const FRPG_ItemSocket& Socket : Instance.Sockets)
					{
						if (Socket.IsOpen())
						{
							continue;
						}
						if (const URPG_AffixDefinition* GemAffix =
							Registry->Find<URPG_AffixDefinition>(Socket.SocketedItemTag))
						{
							FRandomStream GemStream(GetTypeHash(Socket.SocketedItemTag));
							const FRPG_ItemAffix Rolled = GemAffix->Roll(Instance.ItemLevel, GemStream);
							if (Rolled.IsValidAffix())
							{
								Mods.Add(FSeam_StatMod(
									Rolled.AttributeTag,
									static_cast<uint8>(Rolled.Op),
									static_cast<double>(Rolled.GetMagnitude()),
									SourceTag));
							}
						}
					}
				}
			}
		}
	}

	// LOCAL path: replace this slot's whole derived group (empty array clears it on unequip).
	ISeam_StatModifierSink::Execute_SetDerivedModifierGroup(Sink.GetObject(), SourceTag, Mods);
}

void URPG_EquipSlotsComponent::RecomputeSetBonuses()
{
	// NO AUTHORITY GUARD: pure local derivation.
	TScriptInterface<ISeam_StatModifierSink> Sink = ResolveStatSink();
	if (!Sink.GetObject())
	{
		return;
	}
	CachedStatSink = Sink;

	// Gather all currently-equipped item identity tags.
	TArray<FGameplayTag> EquippedItemTags;
	for (const FGameplayTag& SlotTag : GetOccupiedSlots())
	{
		const FGameplayTag ItemTag = GetEquippedItem(SlotTag);
		if (ItemTag.IsValid())
		{
			EquippedItemTags.Add(ItemTag);
		}
	}

	TArray<FRPG_StatModifier> SetMods;
	for (const TObjectPtr<URPG_EquipmentSetDefinition>& SetPtr : KnownSets)
	{
		const URPG_EquipmentSetDefinition* Set = SetPtr.Get();
		if (!Set)
		{
			continue;
		}
		const int32 Worn = Set->CountWornMembers(EquippedItemTags);
		if (Worn > 0)
		{
			Set->GatherActiveBonuses(Worn, RPG_DepthTags::StatSource_Set, SetMods);
		}
	}

	// Convert to seam mods and publish under the single set source key (replaces atomically).
	TArray<FSeam_StatMod> SeamMods;
	SeamMods.Reserve(SetMods.Num());
	for (const FRPG_StatModifier& Mod : SetMods)
	{
		SeamMods.Add(FSeam_StatMod(
			Mod.AttributeTag,
			static_cast<uint8>(Mod.Op),
			static_cast<double>(Mod.Magnitude),
			RPG_DepthTags::StatSource_Set));
	}

	ISeam_StatModifierSink::Execute_SetDerivedModifierGroup(
		Sink.GetObject(), RPG_DepthTags::StatSource_Set, SeamMods);
}
