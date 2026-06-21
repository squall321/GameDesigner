// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Auction/SimEco_AuctionClientComponent.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"
#include "GameFramework/Actor.h"

USimEco_AuctionClientComponent::USimEco_AuctionClientComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true); // RPC routing only; no replicated state
}

bool USimEco_AuctionClientComponent::OwnerHasAuthority() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}

USimEco_AuctionSubsystem* USimEco_AuctionClientComponent::ResolveAuction() const
{
	return FDP_SubsystemStatics::GetWorldSubsystem<USimEco_AuctionSubsystem>(this);
}

// ---- Client entry points ----

void USimEco_AuctionClientComponent::RequestList(FGameplayTag ItemTag, int32 Quantity, FGameplayTag CurrencyTag,
	int64 MinBid, int64 BuyoutPrice, int32 DurationDays)
{
	if (OwnerHasAuthority())
	{
		USimEco_AuctionSubsystem* Auction = ResolveAuction();
		int32 LotId = INDEX_NONE;
		const ESimEco_AuctionResult R = Auction
			? Auction->ListItem(GetOwner(), ItemTag, FMath::Clamp(Quantity, 1, FMath::Max(1, MaxListQuantity)),
				CurrencyTag, MinBid, BuyoutPrice, DurationDays, LotId)
			: ESimEco_AuctionResult::NoSuchLot;
		OnAuctionResult.Broadcast(R, LotId);
		return;
	}
	Server_List(ItemTag, Quantity, CurrencyTag, MinBid, BuyoutPrice, DurationDays);
}

void USimEco_AuctionClientComponent::RequestBid(int32 LotId, int64 Amount)
{
	if (OwnerHasAuthority())
	{
		USimEco_AuctionSubsystem* Auction = ResolveAuction();
		const ESimEco_AuctionResult R = Auction ? Auction->PlaceBid(GetOwner(), LotId, Amount) : ESimEco_AuctionResult::NoSuchLot;
		OnAuctionResult.Broadcast(R, LotId);
		return;
	}
	Server_Bid(LotId, Amount);
}

void USimEco_AuctionClientComponent::RequestBuyout(int32 LotId)
{
	if (OwnerHasAuthority())
	{
		USimEco_AuctionSubsystem* Auction = ResolveAuction();
		const ESimEco_AuctionResult R = Auction ? Auction->Buyout(GetOwner(), LotId) : ESimEco_AuctionResult::NoSuchLot;
		OnAuctionResult.Broadcast(R, LotId);
		return;
	}
	Server_Buyout(LotId);
}

void USimEco_AuctionClientComponent::RequestCancel(int32 LotId)
{
	if (OwnerHasAuthority())
	{
		USimEco_AuctionSubsystem* Auction = ResolveAuction();
		const ESimEco_AuctionResult R = Auction ? Auction->CancelLot(GetOwner(), LotId) : ESimEco_AuctionResult::NoSuchLot;
		OnAuctionResult.Broadcast(R, LotId);
		return;
	}
	Server_Cancel(LotId);
}

// ---- Server RPCs ----

bool USimEco_AuctionClientComponent::Server_List_Validate(FGameplayTag ItemTag, int32 Quantity, FGameplayTag CurrencyTag,
	int64 MinBid, int64 BuyoutPrice, int32 DurationDays)
{
	return ItemTag.IsValid() && Quantity > 0 && CurrencyTag.IsValid() && MinBid >= 0 && BuyoutPrice >= 0 && DurationDays > 0;
}

void USimEco_AuctionClientComponent::Server_List_Implementation(FGameplayTag ItemTag, int32 Quantity, FGameplayTag CurrencyTag,
	int64 MinBid, int64 BuyoutPrice, int32 DurationDays)
{
	USimEco_AuctionSubsystem* Auction = ResolveAuction();
	int32 LotId = INDEX_NONE;
	const ESimEco_AuctionResult R = Auction
		? Auction->ListItem(GetOwner(), ItemTag, FMath::Clamp(Quantity, 1, FMath::Max(1, MaxListQuantity)),
			CurrencyTag, MinBid, BuyoutPrice, DurationDays, LotId)
		: ESimEco_AuctionResult::NoSuchLot;
	Client_AuctionResult(R, LotId);
}

bool USimEco_AuctionClientComponent::Server_Bid_Validate(int32 LotId, int64 Amount) { return LotId != INDEX_NONE && Amount > 0; }
void USimEco_AuctionClientComponent::Server_Bid_Implementation(int32 LotId, int64 Amount)
{
	USimEco_AuctionSubsystem* Auction = ResolveAuction();
	const ESimEco_AuctionResult R = Auction ? Auction->PlaceBid(GetOwner(), LotId, Amount) : ESimEco_AuctionResult::NoSuchLot;
	Client_AuctionResult(R, LotId);
}

bool USimEco_AuctionClientComponent::Server_Buyout_Validate(int32 LotId) { return LotId != INDEX_NONE; }
void USimEco_AuctionClientComponent::Server_Buyout_Implementation(int32 LotId)
{
	USimEco_AuctionSubsystem* Auction = ResolveAuction();
	const ESimEco_AuctionResult R = Auction ? Auction->Buyout(GetOwner(), LotId) : ESimEco_AuctionResult::NoSuchLot;
	Client_AuctionResult(R, LotId);
}

bool USimEco_AuctionClientComponent::Server_Cancel_Validate(int32 LotId) { return LotId != INDEX_NONE; }
void USimEco_AuctionClientComponent::Server_Cancel_Implementation(int32 LotId)
{
	USimEco_AuctionSubsystem* Auction = ResolveAuction();
	const ESimEco_AuctionResult R = Auction ? Auction->CancelLot(GetOwner(), LotId) : ESimEco_AuctionResult::NoSuchLot;
	Client_AuctionResult(R, LotId);
}

void USimEco_AuctionClientComponent::Client_AuctionResult_Implementation(ESimEco_AuctionResult Result, int32 LotId)
{
	OnAuctionResult.Broadcast(Result, LotId);
}
