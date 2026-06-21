// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Inventory/RPG_EncumbranceComponent.h"
#include "Inventory/RPG_EncumbranceCurve.h"
#include "Inventory/RPG_InventoryComponent.h"
#include "Inventory/RPG_ItemInstanceComponent.h"
#include "Item/RPG_ItemDefinition.h"
#include "Item/RPG_ItemInstance.h"
#include "Item/RPG_DepthTags.h"
#include "Stats/RPG_StatsComponent.h"
#include "Stats/Seam_StatModifierSink.h"
#include "Stats/Seam_StatMod.h"
#include "Data/DPDataRegistrySubsystem.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"
#include "GameFramework/Actor.h"

URPG_EncumbranceComponent::URPG_EncumbranceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	// No replicated state of its own: it derives everything from already-replicated weight sources, so it
	// does not need to replicate. It still runs on both server and clients.
	SetIsReplicatedByDefault(false);
}

void URPG_EncumbranceComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!bBoundDelegates)
	{
		if (URPG_InventoryComponent* Inventory = ResolveInventory())
		{
			Inventory->OnInventoryChanged.AddDynamic(this, &URPG_EncumbranceComponent::HandleInventoryChanged);
		}
		if (URPG_ItemInstanceComponent* InstanceComp = ResolveInstanceComponent())
		{
			InstanceComp->OnInstancesChanged.AddDynamic(this, &URPG_EncumbranceComponent::HandleInstancesChanged);
		}
		bBoundDelegates = true;
	}

	RecalculateEncumbrance();
}

void URPG_EncumbranceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bBoundDelegates)
	{
		if (URPG_InventoryComponent* Inventory = ResolveInventory())
		{
			Inventory->OnInventoryChanged.RemoveDynamic(this, &URPG_EncumbranceComponent::HandleInventoryChanged);
		}
		if (URPG_ItemInstanceComponent* InstanceComp = ResolveInstanceComponent())
		{
			InstanceComp->OnInstancesChanged.RemoveDynamic(this, &URPG_EncumbranceComponent::HandleInstancesChanged);
		}
		bBoundDelegates = false;
	}
	Super::EndPlay(EndPlayReason);
}

URPG_InventoryComponent* URPG_EncumbranceComponent::ResolveInventory() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<URPG_InventoryComponent>() : nullptr;
}

URPG_ItemInstanceComponent* URPG_EncumbranceComponent::ResolveInstanceComponent() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<URPG_ItemInstanceComponent>() : nullptr;
}

TScriptInterface<ISeam_StatModifierSink> URPG_EncumbranceComponent::ResolveStatSink() const
{
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

float URPG_EncumbranceComponent::ResolveStrength() const
{
	// The Strength attribute is read off the stats component THROUGH its concrete getter when present. We
	// avoid a hard dependency by reading via the sink object cast to URPG_StatsComponent only if that is the
	// implementer; otherwise strength is treated as 0 (base capacity only). This keeps the curve usable even
	// when no stats component is attached.
	if (const AActor* Owner = GetOwner())
	{
		if (const URPG_StatsComponent* Stats = Owner->FindComponentByClass<URPG_StatsComponent>())
		{
			return Stats->GetAttributeValue(RPG_DepthTags::Attribute_Strength);
		}
	}
	return 0.f;
}

float URPG_EncumbranceComponent::GetCarriedWeight() const
{
	float Total = 0.f;

	if (const URPG_InventoryComponent* Inventory = ResolveInventory())
	{
		Total += Inventory->GetTotalWeight();
	}

	if (const URPG_ItemInstanceComponent* InstanceComp = ResolveInstanceComponent())
	{
		UDP_DataRegistrySubsystem* Registry =
			FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_DataRegistrySubsystem>(this);
		for (const FRPG_ItemInstance& Instance : InstanceComp->GetAllInstances())
		{
			float UnitWeight = 0.f;
			if (Registry)
			{
				if (const URPG_ItemDefinition* Def = Registry->Find<URPG_ItemDefinition>(Instance.ItemTag))
				{
					UnitWeight = Def->Weight;
				}
			}
			Total += UnitWeight; // each rolled instance is a single non-stackable unit
		}
	}

	return Total;
}

float URPG_EncumbranceComponent::GetCapacity() const
{
	if (!Tiers)
	{
		return 0.f;
	}
	return Tiers->GetCapacityForStrength(ResolveStrength());
}

void URPG_EncumbranceComponent::RecalculateEncumbrance()
{
	// NO AUTHORITY GUARD: pure local derivation from replicated weight sources. Runs on server AND clients.
	TScriptInterface<ISeam_StatModifierSink> Sink = ResolveStatSink();
	if (!Sink.GetObject() || !Tiers)
	{
		return;
	}

	const float Capacity = GetCapacity();
	const float Carried = GetCarriedWeight();
	const float LoadFraction = (Capacity > 0.f) ? (Carried / Capacity) : 0.f;

	float MoveSpeedMultiplier = 0.f;
	const FGameplayTag NewTier = Tiers->ResolveTier(LoadFraction, MoveSpeedMultiplier);

	// Publish the move-speed penalty (if any) under the encumbrance source key via the LOCAL path.
	TArray<FSeam_StatMod> Mods;
	if (!FMath::IsNearlyZero(MoveSpeedMultiplier))
	{
		Mods.Add(FSeam_StatMod(
			RPG_DepthTags::Attribute_MoveSpeedMult,
			static_cast<uint8>(ERPG_StatModOp::Multiplicative),
			static_cast<double>(MoveSpeedMultiplier),
			RPG_DepthTags::StatSource_Encumbrance));
	}
	ISeam_StatModifierSink::Execute_SetDerivedModifierGroup(
		Sink.GetObject(), RPG_DepthTags::StatSource_Encumbrance, Mods);

	if (NewTier != CurrentTier)
	{
		CurrentTier = NewTier;
		OnEncumbranceChanged.Broadcast(this, CurrentTier);
		UE_LOG(LogDPData, Verbose, TEXT("[RPG_Encumbrance] Tier -> %s (load %.2f)"),
			*CurrentTier.ToString(), LoadFraction);
	}
}

void URPG_EncumbranceComponent::HandleInventoryChanged(URPG_InventoryComponent* /*Inventory*/)
{
	RecalculateEncumbrance();
}

void URPG_EncumbranceComponent::HandleInstancesChanged(URPG_ItemInstanceComponent* /*Component*/)
{
	RecalculateEncumbrance();
}
