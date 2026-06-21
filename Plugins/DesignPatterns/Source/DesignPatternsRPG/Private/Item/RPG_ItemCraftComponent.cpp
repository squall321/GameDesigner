// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Item/RPG_ItemCraftComponent.h"
#include "Item/RPG_CraftCostTable.h"
#include "Item/RPG_ItemRoller.h"
#include "Item/RPG_ItemInstance.h"
#include "Item/RPG_AffixDefinition.h"
#include "Item/RPG_DepthTags.h"
#include "Inventory/RPG_ItemInstanceComponent.h"
#include "Inventory/RPG_InventoryComponent.h"
#include "Items/Seam_ItemDurability.h"
#include "Economy/Seam_Wallet.h"
#include "Data/DPDataRegistrySubsystem.h"
#include "Item/RPG_ItemDefinition.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"
#include "GameFramework/Actor.h"

URPG_ItemCraftComponent::URPG_ItemCraftComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	// Replicated so the owning client's Server...WithValidation RPCs route to the server.
	SetIsReplicatedByDefault(true);
}

URPG_ItemInstanceComponent* URPG_ItemCraftComponent::ResolveInstanceComponent() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<URPG_ItemInstanceComponent>() : nullptr;
}

URPG_InventoryComponent* URPG_ItemCraftComponent::ResolveInventory() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<URPG_InventoryComponent>() : nullptr;
}

TScriptInterface<ISeam_ItemDurability> URPG_ItemCraftComponent::ResolveDurabilitySeam(int32 InstanceId) const
{
	TScriptInterface<ISeam_ItemDurability> Result;
	if (const AActor* Owner = GetOwner())
	{
		TArray<UActorComponent*> Components;
		Owner->GetComponents(Components);
		for (UActorComponent* Comp : Components)
		{
			if (Comp && Comp->GetClass()->ImplementsInterface(USeam_ItemDurability::StaticClass()))
			{
				// Only accept a backend that actually tracks this instance; otherwise fall back to the field.
				if (ISeam_ItemDurability::Execute_TracksInstance(Comp, InstanceId))
				{
					Result.SetObject(Comp);
					Result.SetInterface(Cast<ISeam_ItemDurability>(Comp));
					break;
				}
			}
		}
	}
	return Result;
}

TScriptInterface<ISeam_Wallet> URPG_ItemCraftComponent::ResolveWallet() const
{
	TScriptInterface<ISeam_Wallet> Result;
	if (const AActor* Owner = GetOwner())
	{
		TArray<UActorComponent*> Components;
		Owner->GetComponents(Components);
		for (UActorComponent* Comp : Components)
		{
			if (Comp && Comp->GetClass()->ImplementsInterface(USeam_Wallet::StaticClass()))
			{
				Result.SetObject(Comp);
				Result.SetInterface(Cast<ISeam_Wallet>(Comp));
				break;
			}
		}
	}
	return Result;
}

bool URPG_ItemCraftComponent::CanAffordCost(const FRPG_CraftCostRow& Cost) const
{
	// Materials.
	if (const URPG_InventoryComponent* Inventory = ResolveInventory())
	{
		for (const FRPG_ItemStack& Mat : Cost.Materials)
		{
			if (Mat.IsValidStack() && Inventory->GetItemCount_Implementation(Mat.ItemTag) < Mat.Count)
			{
				return false;
			}
		}
	}
	else if (Cost.Materials.Num() > 0)
	{
		return false; // a material cost with no inventory cannot be paid
	}

	// Currency (read-only check; debit is the wallet's responsibility).
	if (Cost.CurrencyTag.IsValid() && Cost.CurrencyCost > 0)
	{
		TScriptInterface<ISeam_Wallet> Wallet = ResolveWallet();
		if (!Wallet.GetObject())
		{
			return false;
		}
		if (!ISeam_Wallet::Execute_CanAfford(Wallet.GetObject(), Cost.CurrencyTag, Cost.CurrencyCost))
		{
			return false;
		}
	}
	return true;
}

void URPG_ItemCraftComponent::ConsumeMaterials(const TArray<FRPG_ItemStack>& Materials)
{
	if (URPG_InventoryComponent* Inventory = ResolveInventory())
	{
		for (const FRPG_ItemStack& Mat : Materials)
		{
			if (Mat.IsValidStack())
			{
				Inventory->RemoveItem_Implementation(Mat.ItemTag, Mat.Count);
			}
		}
	}
}

bool URPG_ItemCraftComponent::ApplyUpgrade_Authority(FRPG_ItemInstance& Inst)
{
	Inst.UpgradeLevel += 1;
	return true;
}

//~ Upgrade -----------------------------------------------------------------------------------

bool URPG_ItemCraftComponent::ServerUpgradeInstance_Validate(int32 InstanceId)
{
	return InstanceId != 0;
}

void URPG_ItemCraftComponent::ServerUpgradeInstance_Implementation(int32 InstanceId)
{
	// AUTHORITY GUARD at the TOP (defensive: Server RPC already runs on the server).
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	URPG_ItemInstanceComponent* InstanceComp = ResolveInstanceComponent();
	if (!InstanceComp || !CostTable)
	{
		return;
	}

	FRPG_ItemInstance Snapshot;
	if (!InstanceComp->GetInstance(InstanceId, Snapshot))
	{
		return;
	}

	FRPG_CraftCostRow Cost;
	if (!CostTable->GetUpgradeCost(Snapshot.RarityTag, Snapshot.UpgradeLevel, Cost) || !CanAffordCost(Cost))
	{
		return;
	}

	ConsumeMaterials(Cost.Materials);
	InstanceComp->MutateInstance(InstanceId, [this](FRPG_ItemInstance& Inst)
	{
		ApplyUpgrade_Authority(Inst);
	});
	UE_LOG(LogDPData, Verbose, TEXT("[RPG_Craft] Upgraded instance %d"), InstanceId);
}

//~ Reroll ------------------------------------------------------------------------------------

bool URPG_ItemCraftComponent::ServerRerollAffix_Validate(int32 InstanceId, int32 AffixIndex)
{
	return InstanceId != 0 && AffixIndex >= 0;
}

void URPG_ItemCraftComponent::ServerRerollAffix_Implementation(int32 InstanceId, int32 AffixIndex)
{
	// AUTHORITY GUARD.
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	URPG_ItemInstanceComponent* InstanceComp = ResolveInstanceComponent();
	if (!InstanceComp || !CostTable || !Roller)
	{
		return;
	}

	FRPG_ItemInstance Snapshot;
	if (!InstanceComp->GetInstance(InstanceId, Snapshot) || !Snapshot.Affixes.IsValidIndex(AffixIndex))
	{
		return;
	}

	FRPG_CraftCostRow Cost;
	if (!CostTable->GetRerollCost(Snapshot.RarityTag, Cost) || !CanAffordCost(Cost))
	{
		return;
	}

	// Resolve the affix definition by its stored tag and re-roll it with a fresh server-drawn seed so the
	// outcome is deterministic given that seed but unpredictable to the client.
	const FGameplayTag AffixDefTag = Snapshot.Affixes[AffixIndex].AffixDefTag;
	const URPG_AffixDefinition* AffixDef = nullptr;
	if (UDP_DataRegistrySubsystem* Registry =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_DataRegistrySubsystem>(this))
	{
		AffixDef = Registry->Find<URPG_AffixDefinition>(AffixDefTag);
	}
	if (!AffixDef)
	{
		return;
	}

	ConsumeMaterials(Cost.Materials);

	const int32 Seed = FMath::Rand() ^ (InstanceId * 2654435761u) ^ (AffixIndex << 8);
	FRandomStream Stream(Seed);
	const FRPG_ItemAffix Rerolled = AffixDef->Roll(Snapshot.ItemLevel, Stream);

	InstanceComp->MutateInstance(InstanceId, [AffixIndex, &Rerolled](FRPG_ItemInstance& Inst)
	{
		if (Inst.Affixes.IsValidIndex(AffixIndex))
		{
			Inst.Affixes[AffixIndex] = Rerolled;
		}
	});
	UE_LOG(LogDPData, Verbose, TEXT("[RPG_Craft] Rerolled affix %d on instance %d"), AffixIndex, InstanceId);
}

//~ Salvage -----------------------------------------------------------------------------------

bool URPG_ItemCraftComponent::ServerSalvageInstance_Validate(int32 InstanceId)
{
	return InstanceId != 0;
}

void URPG_ItemCraftComponent::ServerSalvageInstance_Implementation(int32 InstanceId)
{
	// AUTHORITY GUARD.
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	URPG_ItemInstanceComponent* InstanceComp = ResolveInstanceComponent();
	if (!InstanceComp || !CostTable)
	{
		return;
	}

	FRPG_ItemInstance Snapshot;
	if (!InstanceComp->GetInstance(InstanceId, Snapshot))
	{
		return;
	}

	TArray<FRPG_ItemStack> Yield;
	CostTable->GetSalvageYield(Snapshot.RarityTag, Yield);

	// Destroy the instance first, then grant materials, so capacity-bound inventories don't see both at once.
	InstanceComp->RemoveInstance(InstanceId);
	if (URPG_InventoryComponent* Inventory = ResolveInventory())
	{
		for (const FRPG_ItemStack& Mat : Yield)
		{
			if (Mat.IsValidStack())
			{
				Inventory->AddItem(Mat.ItemTag, Mat.Count);
			}
		}
	}
	UE_LOG(LogDPData, Verbose, TEXT("[RPG_Craft] Salvaged instance %d (%d material stacks)"),
		InstanceId, Yield.Num());
}

//~ Repair ------------------------------------------------------------------------------------

bool URPG_ItemCraftComponent::ServerRepairInstance_Validate(int32 InstanceId, float Amount)
{
	return InstanceId != 0 && Amount > 0.f;
}

void URPG_ItemCraftComponent::ServerRepairInstance_Implementation(int32 InstanceId, float Amount)
{
	// AUTHORITY GUARD.
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	URPG_ItemInstanceComponent* InstanceComp = ResolveInstanceComponent();
	if (!InstanceComp)
	{
		return;
	}

	// Prefer a durability backend that tracks this instance; otherwise fall back to the instance field.
	TScriptInterface<ISeam_ItemDurability> Durability = ResolveDurabilitySeam(InstanceId);
	if (Durability.GetObject())
	{
		ISeam_ItemDurability::Execute_Repair(Durability.GetObject(), InstanceId, Amount);
		return;
	}

	InstanceComp->MutateInstance(InstanceId, [Amount](FRPG_ItemInstance& Inst)
	{
		Inst.CurrentDurability = FMath::Clamp(Inst.CurrentDurability + Amount, 0.f, Inst.MaxDurability);
	});
	UE_LOG(LogDPData, Verbose, TEXT("[RPG_Craft] Repaired instance %d by %.1f (field fallback)"),
		InstanceId, Amount);
}
