// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Auction/SimEco_AuctionReplicationProxy.h"
#include "Core/DPLog.h"
#include "Net/UnrealNetwork.h"

//~ Fast-array item callbacks (clients) -------------------------------------------------------
void FSimEco_AuctionLot::PostReplicatedAdd(const FSimEco_AuctionLotArray& In)    { if (In.OwnerProxy) In.OwnerProxy->HandleReplicatedChange(); }
void FSimEco_AuctionLot::PostReplicatedChange(const FSimEco_AuctionLotArray& In) { if (In.OwnerProxy) In.OwnerProxy->HandleReplicatedChange(); }
void FSimEco_AuctionLot::PreReplicatedRemove(const FSimEco_AuctionLotArray& In)  { if (In.OwnerProxy) In.OwnerProxy->HandleReplicatedChange(); }

//~ ASimEco_AuctionReplicationProxy ----------------------------------------------------------

ASimEco_AuctionReplicationProxy::ASimEco_AuctionReplicationProxy()
{
	bReplicates = true;
	bAlwaysRelevant = true;            // the auction board is relevant to every connection
	SetReplicatingMovement(false);
	PrimaryActorTick.bCanEverTick = false;
	NetUpdateFrequency = 5.0f;         // lots change on bids/settlement, not per frame

	LotSummaries.OwnerProxy = this;
}

void ASimEco_AuctionReplicationProxy::PostInitProperties()
{
	Super::PostInitProperties();
	LotSummaries.OwnerProxy = this;
}

void ASimEco_AuctionReplicationProxy::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASimEco_AuctionReplicationProxy, LotSummaries);
}

int32 ASimEco_AuctionReplicationProxy::FindLotIndex(int32 LotId) const
{
	for (int32 i = 0; i < LotSummaries.Lots.Num(); ++i)
	{
		if (LotSummaries.Lots[i].LotId == LotId)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

const FSimEco_AuctionLot* ASimEco_AuctionReplicationProxy::FindLot(int32 LotId) const
{
	const int32 Idx = FindLotIndex(LotId);
	return (Idx != INDEX_NONE) ? &LotSummaries.Lots[Idx] : nullptr;
}

void ASimEco_AuctionReplicationProxy::UpsertLot(const FSimEco_AuctionLot& Lot)
{
	if (!HasAuthority())
	{
		return;
	}
	const int32 Idx = FindLotIndex(Lot.LotId);
	if (Idx != INDEX_NONE)
	{
		FSimEco_AuctionLot& Existing = LotSummaries.Lots[Idx];
		Existing = Lot;
		LotSummaries.MarkItemDirty(Existing);
	}
	else
	{
		FSimEco_AuctionLot& Added = LotSummaries.Lots.Add_GetRef(Lot);
		LotSummaries.MarkItemDirty(Added);
	}
	OnAuctionLotsChanged.Broadcast(this);
}

void ASimEco_AuctionReplicationProxy::RemoveLot(int32 LotId)
{
	if (!HasAuthority())
	{
		return;
	}
	const int32 Idx = FindLotIndex(LotId);
	if (Idx != INDEX_NONE)
	{
		LotSummaries.Lots.RemoveAt(Idx);
		LotSummaries.MarkArrayDirty();
		OnAuctionLotsChanged.Broadcast(this);
	}
}

void ASimEco_AuctionReplicationProxy::HandleReplicatedChange()
{
	OnAuctionLotsChanged.Broadcast(this);
}
