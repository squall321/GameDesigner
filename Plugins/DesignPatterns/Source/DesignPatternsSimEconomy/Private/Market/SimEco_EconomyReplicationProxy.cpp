// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Market/SimEco_EconomyReplicationProxy.h"
#include "Core/DPLog.h"
#include "Net/UnrealNetwork.h"

//~ FSimEco_PriceEntry replication callbacks (client side) ------------------------------------

void FSimEco_PriceEntry::PostReplicatedAdd(const FSimEco_PriceArray& InArraySerializer)
{
	if (InArraySerializer.OwnerProxy)
	{
		InArraySerializer.OwnerProxy->HandleReplicatedChange();
	}
}

void FSimEco_PriceEntry::PostReplicatedChange(const FSimEco_PriceArray& InArraySerializer)
{
	if (InArraySerializer.OwnerProxy)
	{
		InArraySerializer.OwnerProxy->HandleReplicatedChange();
	}
}

void FSimEco_PriceEntry::PreReplicatedRemove(const FSimEco_PriceArray& InArraySerializer)
{
	if (InArraySerializer.OwnerProxy)
	{
		InArraySerializer.OwnerProxy->HandleReplicatedChange();
	}
}

//~ ASimEco_EconomyReplicationProxy -----------------------------------------------------------

ASimEco_EconomyReplicationProxy::ASimEco_EconomyReplicationProxy()
{
	bReplicates = true;
	bAlwaysRelevant = true;            // a price summary is relevant to every connection
	SetReplicatingMovement(false);     // an AInfo never moves
	PrimaryActorTick.bCanEverTick = false;
	NetUpdateFrequency = 5.0f;         // prices update on the economy step, not per frame

	// Wire the fast-array back-pointer so entry callbacks can notify us (server and client).
	Prices.OwnerProxy = this;
}

void ASimEco_EconomyReplicationProxy::PostInitProperties()
{
	Super::PostInitProperties();
	// Defensive re-wire: the back-pointer must survive any default-subobject re-init.
	Prices.OwnerProxy = this;
}

void ASimEco_EconomyReplicationProxy::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASimEco_EconomyReplicationProxy, Prices);
}

FSimEco_PriceEntry* ASimEco_EconomyReplicationProxy::FindEntry(const FGameplayTag& CommodityTag)
{
	return Prices.Entries.FindByPredicate(
		[&CommodityTag](const FSimEco_PriceEntry& E) { return E.CommodityTag == CommodityTag; });
}

const FSimEco_PriceEntry* ASimEco_EconomyReplicationProxy::FindEntry(const FGameplayTag& CommodityTag) const
{
	return Prices.Entries.FindByPredicate(
		[&CommodityTag](const FSimEco_PriceEntry& E) { return E.CommodityTag == CommodityTag; });
}

bool ASimEco_EconomyReplicationProxy::SyncFromServer(const FGameplayTag& CommodityTag, double NewPrice, double Epsilon)
{
	// AUTHORITY GUARD: only the server mutates replicated price state.
	if (!HasAuthority())
	{
		return false;
	}
	if (!CommodityTag.IsValid())
	{
		return false;
	}

	const double SafeEpsilon = FMath::Max(0.0, Epsilon);

	if (FSimEco_PriceEntry* Existing = FindEntry(CommodityTag))
	{
		const double Prev = Existing->Price.FloatValue;
		if (FMath::Abs(NewPrice - Prev) < SafeEpsilon)
		{
			// Sub-epsilon wobble: do not dirty, do not spend bandwidth.
			return false;
		}
		Existing->Price = FSeam_NetValue::MakeFloat(NewPrice);
		Prices.MarkItemDirty(*Existing);
	}
	else
	{
		FSimEco_PriceEntry& Added = Prices.Entries.AddDefaulted_GetRef();
		Added.CommodityTag = CommodityTag;
		Added.Price = FSeam_NetValue::MakeFloat(NewPrice);
		Prices.MarkItemDirty(Added);
	}

	// Server-side listeners (e.g. server UI) get the same notification path as clients.
	OnPriceSummaryChanged.Broadcast(this);
	return true;
}

double ASimEco_EconomyReplicationProxy::GetReplicatedPrice(FGameplayTag CommodityTag) const
{
	if (const FSimEco_PriceEntry* Entry = FindEntry(CommodityTag))
	{
		return Entry->Price.FloatValue;
	}
	return 0.0;
}

bool ASimEco_EconomyReplicationProxy::HasPrice(FGameplayTag CommodityTag) const
{
	return FindEntry(CommodityTag) != nullptr;
}

void ASimEco_EconomyReplicationProxy::GetAllPrices(TArray<FGameplayTag>& OutCommodities, TArray<double>& OutPrices) const
{
	OutCommodities.Reset(Prices.Entries.Num());
	OutPrices.Reset(Prices.Entries.Num());
	for (const FSimEco_PriceEntry& Entry : Prices.Entries)
	{
		OutCommodities.Add(Entry.CommodityTag);
		OutPrices.Add(Entry.Price.FloatValue);
	}
}

void ASimEco_EconomyReplicationProxy::HandleReplicatedChange()
{
	OnPriceSummaryChanged.Broadcast(this);
}
