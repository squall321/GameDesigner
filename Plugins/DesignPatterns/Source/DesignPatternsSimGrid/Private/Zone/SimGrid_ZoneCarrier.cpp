// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Zone/SimGrid_ZoneCarrier.h"
#include "Settings/SimGrid_FeatureSettings.h"
#include "SimGrid_NativeTags.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"

//~ Fast-array item callbacks (clients only) ----------------------------------------------------

void FSimGrid_ZoneEntry::PreReplicatedRemove(const FSimGrid_ZoneArray& InArraySerializer)
{
	if (InArraySerializer.OwnerCarrier)
	{
		InArraySerializer.OwnerCarrier->HandleReplicatedZoneChange(Cell);
	}
}

void FSimGrid_ZoneEntry::PostReplicatedAdd(const FSimGrid_ZoneArray& InArraySerializer)
{
	if (InArraySerializer.OwnerCarrier)
	{
		InArraySerializer.OwnerCarrier->HandleReplicatedZoneChange(Cell);
	}
}

void FSimGrid_ZoneEntry::PostReplicatedChange(const FSimGrid_ZoneArray& InArraySerializer)
{
	if (InArraySerializer.OwnerCarrier)
	{
		InArraySerializer.OwnerCarrier->HandleReplicatedZoneChange(Cell);
	}
}

//~ Construction / lifecycle --------------------------------------------------------------------

ASimGrid_ZoneCarrier::ASimGrid_ZoneCarrier()
{
	bReplicates = true;
	bAlwaysRelevant = true; // the zone map is global; keep it relevant to all viewers
	SetReplicatingMovement(false);
	NetUpdateFrequency = 10.f;
	NetDormancy = DORM_Initial; // idle until a paint changes state
	PrimaryActorTick.bCanEverTick = false;
}

void ASimGrid_ZoneCarrier::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	// Wire the back-pointer on both server and client so item callbacks can notify us.
	Zones.OwnerCarrier = this;
}

void ASimGrid_ZoneCarrier::BeginPlay()
{
	Super::BeginPlay();
	Zones.OwnerCarrier = this;
	RegisterService();
}

void ASimGrid_ZoneCarrier::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnregisterService();
	Super::EndPlay(EndPlayReason);
}

void ASimGrid_ZoneCarrier::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASimGrid_ZoneCarrier, Zones);
}

//~ Service registration ------------------------------------------------------------------------

void ASimGrid_ZoneCarrier::RegisterService()
{
	if (const USimGrid_FeatureSettings* Features = USimGrid_FeatureSettings::Get())
	{
		RegisteredServiceTag = Features->ZoneCarrierServiceTag;
	}
	if (!RegisteredServiceTag.IsValid())
	{
		RegisteredServiceTag = SimGridTags::Service_ZoneCarrier;
	}
	if (UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		// WeakObserved: the locator is GI-scoped and must not keep a dead world's actor alive.
		Locator->RegisterService(RegisteredServiceTag, this, EDP_ServiceLifetime::WeakObserved, /*bAllowOverride*/ true);
	}
}

void ASimGrid_ZoneCarrier::UnregisterService()
{
	if (RegisteredServiceTag.IsValid())
	{
		if (UDP_ServiceLocatorSubsystem* Locator =
			FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
		{
			Locator->UnregisterService(RegisteredServiceTag);
		}
	}
}

ASimGrid_ZoneCarrier* ASimGrid_ZoneCarrier::Resolve(const UObject* WorldContextObject)
{
	FGameplayTag Key = SimGridTags::Service_ZoneCarrier;
	if (const USimGrid_FeatureSettings* Features = USimGrid_FeatureSettings::Get())
	{
		if (Features->ZoneCarrierServiceTag.IsValid())
		{
			Key = Features->ZoneCarrierServiceTag;
		}
	}
	if (const UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(WorldContextObject))
	{
		return Cast<ASimGrid_ZoneCarrier>(Locator->ResolveService(Key));
	}
	return nullptr;
}

//~ Mutators (authority only) -------------------------------------------------------------------

void ASimGrid_ZoneCarrier::WakeForChange()
{
	FlushNetDormancy();
}

FSimGrid_ZoneEntry* ASimGrid_ZoneCarrier::FindEntryMutable(const FSeam_CellCoord& Cell)
{
	for (FSimGrid_ZoneEntry& Entry : Zones.Entries)
	{
		if (Entry.Cell == Cell)
		{
			return &Entry;
		}
	}
	return nullptr;
}

const FSimGrid_ZoneEntry* ASimGrid_ZoneCarrier::FindEntry(const FSeam_CellCoord& Cell) const
{
	for (const FSimGrid_ZoneEntry& Entry : Zones.Entries)
	{
		if (Entry.Cell == Cell)
		{
			return &Entry;
		}
	}
	return nullptr;
}

bool ASimGrid_ZoneCarrier::PaintZone(const FSeam_CellCoord& Cell, FGameplayTag ZoneTypeTag, const FSeam_EntityId& OwnerId)
{
	if (!HasAuthority())
	{
		return false;
	}
	if (!ZoneTypeTag.IsValid())
	{
		return false;
	}

	if (FSimGrid_ZoneEntry* Existing = FindEntryMutable(Cell))
	{
		const bool bTypeChanged = (Existing->ZoneTypeTag != ZoneTypeTag);
		const bool bOwnerChanged = (Existing->OwnerId != OwnerId);
		if (!bTypeChanged && !bOwnerChanged)
		{
			return false; // no-op
		}
		Existing->ZoneTypeTag = ZoneTypeTag;
		Existing->OwnerId = OwnerId;
		if (bTypeChanged)
		{
			Existing->GrowthLevel = 0.f; // re-zoning restarts development
		}
		Zones.MarkItemDirty(*Existing);
	}
	else
	{
		FSimGrid_ZoneEntry NewEntry(Cell, ZoneTypeTag, OwnerId);
		FSimGrid_ZoneEntry& Added = Zones.Entries.Add_GetRef(NewEntry);
		Zones.MarkItemDirty(Added);
	}

	WakeForChange();
	OnZoneChanged.Broadcast(this, Cell);
	return true;
}

bool ASimGrid_ZoneCarrier::EraseZone(const FSeam_CellCoord& Cell)
{
	if (!HasAuthority())
	{
		return false;
	}
	const int32 Index = Zones.Entries.IndexOfByPredicate(
		[&Cell](const FSimGrid_ZoneEntry& E) { return E.Cell == Cell; });
	if (Index == INDEX_NONE)
	{
		return false;
	}
	Zones.Entries.RemoveAt(Index);
	Zones.MarkArrayDirty();
	WakeForChange();
	OnZoneChanged.Broadcast(this, Cell);
	return true;
}

bool ASimGrid_ZoneCarrier::SetZoneGrowth(const FSeam_CellCoord& Cell, float NewGrowth)
{
	if (!HasAuthority())
	{
		return false;
	}
	FSimGrid_ZoneEntry* Entry = FindEntryMutable(Cell);
	if (!Entry)
	{
		return false;
	}
	const float Clamped = FMath::Clamp(NewGrowth, 0.f, 1.f);
	if (FMath::IsNearlyEqual(Entry->GrowthLevel, Clamped))
	{
		return false;
	}
	Entry->GrowthLevel = Clamped;
	Zones.MarkItemDirty(*Entry);
	WakeForChange();
	OnZoneChanged.Broadcast(this, Cell);
	return true;
}

//~ Reads ---------------------------------------------------------------------------------------

FGameplayTag ASimGrid_ZoneCarrier::GetZoneAt(const FSeam_CellCoord& Cell) const
{
	const FSimGrid_ZoneEntry* Entry = FindEntry(Cell);
	return Entry ? Entry->ZoneTypeTag : FGameplayTag();
}

float ASimGrid_ZoneCarrier::GetZoneGrowth(const FSeam_CellCoord& Cell) const
{
	const FSimGrid_ZoneEntry* Entry = FindEntry(Cell);
	return Entry ? Entry->GrowthLevel : 0.f;
}

FSeam_EntityId ASimGrid_ZoneCarrier::GetZoneOwner(const FSeam_CellCoord& Cell) const
{
	const FSimGrid_ZoneEntry* Entry = FindEntry(Cell);
	return Entry ? Entry->OwnerId : FSeam_EntityId::Invalid();
}

void ASimGrid_ZoneCarrier::HandleReplicatedZoneChange(const FSeam_CellCoord& Cell)
{
	OnZoneChanged.Broadcast(this, Cell);
}
