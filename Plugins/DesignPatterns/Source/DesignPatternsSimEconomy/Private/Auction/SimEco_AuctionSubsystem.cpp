// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Auction/SimEco_AuctionSubsystem.h"
#include "Auction/SimEco_AuctionReplicationProxy.h"
#include "Pricing/SimEco_PricingTags.h"
#include "Economy/SimEco_EconomySubsystem.h"
#include "Economy/Seam_WalletAuthority.h"
#include "Economy/Seam_TradableInventory.h"
#include "Economy/Seam_PurchaseTarget.h"
#include "Clock/Seam_SimClock.h"
#include "Identity/Seam_EntityIdentity.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Market/SimEco_EconomyTags.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"
#include "Engine/World.h"

namespace
{
	/** Resolve a UINTERFACE seam off an actor (actor first, then components). */
	UObject* ResolveSeamObj(const AActor* Actor, TSubclassOf<UInterface> SeamClass)
	{
		if (!Actor || !*SeamClass)
		{
			return nullptr;
		}
		if (Actor->GetClass()->ImplementsInterface(SeamClass))
		{
			return const_cast<AActor*>(Actor);
		}
		return const_cast<AActor*>(Actor)->FindComponentByInterface(SeamClass);
	}
}

void USimEco_AuctionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (HasWorldAuthority())
	{
		EnsureProxy();

		// Register with the economy driver so settlement runs on day rollover, AND register ourselves as
		// the auction service so client components resolve us by tag.
		if (USimEco_EconomySubsystem* Eco = ResolveEconomy())
		{
			TScriptInterface<ISimEco_StepListener> Self;
			Self.SetObject(this);
			Self.SetInterface(static_cast<ISimEco_StepListener*>(this));
			Eco->RegisterStepListener(Self);
			bRegisteredWithEconomy = true;
		}
	}

	if (UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		Locator->RegisterService(SimEcoPricingTags::Service_Auction, this, EDP_ServiceLifetime::WeakObserved, /*bAllowOverride*/ true);
	}
}

void USimEco_AuctionSubsystem::Deinitialize()
{
	if (bRegisteredWithEconomy)
	{
		if (USimEco_EconomySubsystem* Eco = ResolveEconomy())
		{
			TScriptInterface<ISimEco_StepListener> Self;
			Self.SetObject(this);
			Self.SetInterface(static_cast<ISimEco_StepListener*>(this));
			Eco->UnregisterStepListener(Self);
		}
		bRegisteredWithEconomy = false;
	}

	if (UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		Locator->UnregisterService(SimEcoPricingTags::Service_Auction);
	}

	if (HasWorldAuthority() && Proxy.IsValid())
	{
		Proxy->Destroy();
	}
	Proxy.Reset();
	Escrow.Reset();
	Lots.Reset();

	Super::Deinitialize();
}

void USimEco_AuctionSubsystem::EnsureProxy()
{
	if (!HasWorldAuthority() || Proxy.IsValid())
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	Proxy = World->SpawnActor<ASimEco_AuctionReplicationProxy>(
		ASimEco_AuctionReplicationProxy::StaticClass(), FTransform::Identity, Params);
	if (!Proxy.IsValid())
	{
		UE_LOG(LogDP, Warning, TEXT("[Auction] Failed to spawn auction replication proxy."));
	}
}

USimEco_EconomySubsystem* USimEco_AuctionSubsystem::ResolveEconomy() const
{
	return FDP_SubsystemStatics::GetWorldSubsystem<USimEco_EconomySubsystem>(this);
}

FSimEco_AuctionLot* USimEco_AuctionSubsystem::FindLot(int32 LotId)
{
	return Lots.FindByPredicate([LotId](const FSimEco_AuctionLot& L) { return L.LotId == LotId; });
}
const FSimEco_AuctionLot* USimEco_AuctionSubsystem::FindLot(int32 LotId) const
{
	return Lots.FindByPredicate([LotId](const FSimEco_AuctionLot& L) { return L.LotId == LotId; });
}

void USimEco_AuctionSubsystem::SyncLotToProxy(const FSimEco_AuctionLot& Lot)
{
	if (Proxy.IsValid())
	{
		Proxy->UpsertLot(Lot);
	}
}

int32 USimEco_AuctionSubsystem::ResolveCurrentDay() const
{
	// Resolve the shared sim clock through the locator key the economy publishes under.
	if (UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		if (UObject* ClockObj = Locator->ResolveService(SimEcoEconomyTags::Service_SimClock))
		{
			if (ClockObj->GetClass()->ImplementsInterface(USeam_SimClock::StaticClass()))
			{
				return ISeam_SimClock::Execute_GetDayNumber(ClockObj);
			}
		}
	}
	return 0;
}

FSeam_EntityId USimEco_AuctionSubsystem::ResolveEntityId(const AActor* Actor)
{
	if (!Actor)
	{
		return FSeam_EntityId::Invalid();
	}
	if (UObject* IdObj = ResolveSeamObj(Actor, USeam_EntityIdentity::StaticClass()))
	{
		return ISeam_EntityIdentity::Execute_GetEntityId(IdObj);
	}
	// No stable identity seam: derive a deterministic id from the actor name so the same actor maps to
	// the same display id within a session (defensive; display-only). Built from a stable string hash
	// so it is engine-version-agnostic (no reliance on FGuid::NewDeterministicGuid, 5.4+).
	const FString Name = Actor->GetFName().ToString();
	const uint32 H = GetTypeHash(Name);
	const uint32 H2 = GetTypeHash(Name + TEXT("|salt"));
	return FSeam_EntityId(FGuid(H, H2, ~H, ~H2));
}

// ----------------------------------------------------------------------------------------------------
// Listing / bidding / buyout (authority-driven)
// ----------------------------------------------------------------------------------------------------

ESimEco_AuctionResult USimEco_AuctionSubsystem::ListItem(AActor* Seller, const FGameplayTag& ItemTag, int32 Quantity,
	const FGameplayTag& CurrencyTag, int64 MinBid, int64 BuyoutPrice, int32 DurationDays, int32& OutLotId)
{
	OutLotId = INDEX_NONE;

	if (!HasWorldAuthority())          return ESimEco_AuctionResult::NotAuthoritative;
	if (!Seller || !ItemTag.IsValid() || Quantity <= 0 || !CurrencyTag.IsValid())
		return ESimEco_AuctionResult::BadRequest;

	UObject* SellerInv = ResolveSeamObj(Seller, USeam_TradableInventory::StaticClass());
	if (!SellerInv || !ISeam_TradableInventory::Execute_CanRemove(SellerInv, ItemTag, Quantity))
	{
		return ESimEco_AuctionResult::CannotEscrowItem;
	}

	// Escrow the goods OUT of the seller's inventory (anti-dupe).
	const int32 Removed = ISeam_TradableInventory::Execute_RemoveItem(SellerInv, ItemTag, Quantity);
	if (Removed < Quantity)
	{
		// Best-effort restore of any partial removal via the add seam.
		if (Removed > 0)
		{
			if (UObject* AddObj = ResolveSeamObj(Seller, USeam_PurchaseTarget::StaticClass()))
			{
				ISeam_PurchaseTarget::Execute_GrantItem(AddObj, ItemTag, Removed);
			}
		}
		return ESimEco_AuctionResult::CannotEscrowItem;
	}

	const int32 LotId = NextLotId++;
	const int32 Day = ResolveCurrentDay();

	FSimEco_AuctionLot Lot;
	Lot.LotId = LotId;
	Lot.ItemTag = ItemTag;
	Lot.Quantity = Quantity;
	Lot.CurrencyTag = CurrencyTag;
	Lot.MinBid = FMath::Max((int64)0, MinBid);
	Lot.CurrentBid = 0;
	Lot.BuyoutPrice = FMath::Max((int64)0, BuyoutPrice);
	Lot.SellerId = ResolveEntityId(Seller);
	Lot.SettlementDay = Day + FMath::Max(1, DurationDays);
	Lot.State = ESimEco_AuctionState::Active;
	Lots.Add(Lot);

	FSimEco_AuctionEscrow E;
	E.Seller = Seller;
	E.ItemTag = ItemTag;
	E.Quantity = Quantity;
	E.CurrencyTag = CurrencyTag;
	Escrow.Add(LotId, E);

	SyncLotToProxy(Lot);
	NotifyAuctionChanged(Seller);
	OutLotId = LotId;
	return ESimEco_AuctionResult::Success;
}

ESimEco_AuctionResult USimEco_AuctionSubsystem::PlaceBid(AActor* Bidder, int32 LotId, int64 Amount)
{
	if (!HasWorldAuthority())  return ESimEco_AuctionResult::NotAuthoritative;
	if (!Bidder || Amount <= 0) return ESimEco_AuctionResult::BadRequest;

	FSimEco_AuctionLot* Lot = FindLot(LotId);
	FSimEco_AuctionEscrow* E = Escrow.Find(LotId);
	if (!Lot || !E || Lot->State != ESimEco_AuctionState::Active)
	{
		return ESimEco_AuctionResult::NoSuchLot;
	}

	// Bid must clear the minimum AND beat the current high bid.
	const int64 Floor = FMath::Max(Lot->MinBid, Lot->CurrentBid + 1);
	if (Amount < Floor)
	{
		return ESimEco_AuctionResult::BidTooLow;
	}

	UObject* Wallet = ResolveSeamObj(Bidder, USeam_WalletAuthority::StaticClass());
	if (!Wallet || !ISeam_WalletAuthority::Execute_CanSpend(Wallet, Lot->CurrencyTag, Amount))
	{
		return ESimEco_AuctionResult::CannotEscrowCurrency;
	}
	if (!ISeam_WalletAuthority::Execute_Spend(Wallet, Lot->CurrencyTag, Amount))
	{
		return ESimEco_AuctionResult::CannotEscrowCurrency;
	}

	// Refund the previous high bidder, then record the new one.
	RefundHighBidder(LotId);

	E->HighBidder = Bidder;
	E->EscrowedCurrency = Amount;
	Lot->CurrentBid = Amount;
	Lot->HighBidderId = ResolveEntityId(Bidder);

	SyncLotToProxy(*Lot);
	NotifyAuctionChanged(Bidder);

	// Auto-settle if the bid met/exceeded buyout.
	if (Lot->BuyoutPrice > 0 && Amount >= Lot->BuyoutPrice)
	{
		SettleLot(LotId, /*bExpired*/ false);
	}
	return ESimEco_AuctionResult::Success;
}

ESimEco_AuctionResult USimEco_AuctionSubsystem::Buyout(AActor* Buyer, int32 LotId)
{
	if (!HasWorldAuthority()) return ESimEco_AuctionResult::NotAuthoritative;
	if (!Buyer)               return ESimEco_AuctionResult::BadRequest;

	FSimEco_AuctionLot* Lot = FindLot(LotId);
	if (!Lot || Lot->State != ESimEco_AuctionState::Active)
	{
		return ESimEco_AuctionResult::NoSuchLot;
	}
	if (Lot->BuyoutPrice <= 0)
	{
		return ESimEco_AuctionResult::NoBuyout;
	}

	// A buyout is just a bid at the buyout price that immediately settles.
	return PlaceBid(Buyer, LotId, Lot->BuyoutPrice);
}

ESimEco_AuctionResult USimEco_AuctionSubsystem::CancelLot(AActor* Seller, int32 LotId)
{
	if (!HasWorldAuthority()) return ESimEco_AuctionResult::NotAuthoritative;

	FSimEco_AuctionLot* Lot = FindLot(LotId);
	FSimEco_AuctionEscrow* E = Escrow.Find(LotId);
	if (!Lot || !E || Lot->State != ESimEco_AuctionState::Active)
	{
		return ESimEco_AuctionResult::NoSuchLot;
	}
	// Only the seller may cancel, and only with no bids.
	if (E->Seller.Get() != Seller || Lot->CurrentBid > 0)
	{
		return ESimEco_AuctionResult::BadRequest;
	}

	// Return goods to the seller and reap the lot.
	if (UObject* AddObj = ResolveSeamObj(Seller, USeam_PurchaseTarget::StaticClass()))
	{
		ISeam_PurchaseTarget::Execute_GrantItem(AddObj, E->ItemTag, E->Quantity);
	}
	Lot->State = ESimEco_AuctionState::Cancelled;
	SyncLotToProxy(*Lot);
	if (Proxy.IsValid()) { Proxy->RemoveLot(LotId); }
	Lots.RemoveAll([LotId](const FSimEco_AuctionLot& L) { return L.LotId == LotId; });
	Escrow.Remove(LotId);
	NotifyAuctionChanged(Seller);
	return ESimEco_AuctionResult::Success;
}

void USimEco_AuctionSubsystem::RefundHighBidder(int32 LotId)
{
	FSimEco_AuctionEscrow* E = Escrow.Find(LotId);
	if (!E || E->EscrowedCurrency <= 0)
	{
		return;
	}
	if (AActor* Prev = E->HighBidder.Get())
	{
		if (UObject* Wallet = ResolveSeamObj(Prev, USeam_WalletAuthority::StaticClass()))
		{
			ISeam_WalletAuthority::Execute_Grant(Wallet, E->CurrencyTag, E->EscrowedCurrency);
		}
	}
	E->EscrowedCurrency = 0;
	E->HighBidder = nullptr;
}

void USimEco_AuctionSubsystem::SettleLot(int32 LotId, bool bExpired)
{
	FSimEco_AuctionLot* Lot = FindLot(LotId);
	FSimEco_AuctionEscrow* E = Escrow.Find(LotId);
	if (!Lot || !E)
	{
		return;
	}

	const bool bHasWinner = !bExpired && E->HighBidder.IsValid() && E->EscrowedCurrency > 0;

	if (bHasWinner)
	{
		// Deliver goods to the winner; pay escrowed currency to the seller.
		if (AActor* Winner = E->HighBidder.Get())
		{
			if (UObject* WinnerInv = ResolveSeamObj(Winner, USeam_PurchaseTarget::StaticClass()))
			{
				ISeam_PurchaseTarget::Execute_GrantItem(WinnerInv, E->ItemTag, E->Quantity);
			}
		}
		if (AActor* Seller = E->Seller.Get())
		{
			if (UObject* SellerWallet = ResolveSeamObj(Seller, USeam_WalletAuthority::StaticClass()))
			{
				ISeam_WalletAuthority::Execute_Grant(SellerWallet, E->CurrencyTag, E->EscrowedCurrency);
			}
		}
		Lot->State = ESimEco_AuctionState::Sold;
	}
	else
	{
		// Expired / no winner: refund any escrowed currency, return goods to the seller.
		RefundHighBidder(LotId);
		if (AActor* Seller = E->Seller.Get())
		{
			if (UObject* SellerInv = ResolveSeamObj(Seller, USeam_PurchaseTarget::StaticClass()))
			{
				ISeam_PurchaseTarget::Execute_GrantItem(SellerInv, E->ItemTag, E->Quantity);
			}
		}
		Lot->State = ESimEco_AuctionState::Expired;
	}

	SyncLotToProxy(*Lot);
	if (Proxy.IsValid()) { Proxy->RemoveLot(LotId); }
	Lots.RemoveAll([LotId](const FSimEco_AuctionLot& L) { return L.LotId == LotId; });
	Escrow.Remove(LotId);
	NotifyAuctionChanged(nullptr);
}

void USimEco_AuctionSubsystem::AdvanceEconomyStep(double /*StepSeconds*/, int64 /*StepIndex*/, int32 DayNumber)
{
	if (!HasWorldAuthority())
	{
		return;
	}
	if (DayNumber == LastSettledDay)
	{
		return; // settle at most once per sim day
	}
	LastSettledDay = DayNumber;

	// Collect lots due for settlement (copy ids first: SettleLot mutates Lots).
	TArray<int32> Due;
	for (const FSimEco_AuctionLot& Lot : Lots)
	{
		if (Lot.State == ESimEco_AuctionState::Active && DayNumber >= Lot.SettlementDay)
		{
			Due.Add(Lot.LotId);
		}
	}
	for (int32 LotId : Due)
	{
		const FSimEco_AuctionEscrow* E = Escrow.Find(LotId);
		const bool bExpired = !(E && E->HighBidder.IsValid() && E->EscrowedCurrency > 0);
		SettleLot(LotId, bExpired);
	}
}

void USimEco_AuctionSubsystem::NotifyAuctionChanged(AActor* Instigator) const
{
	if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		Bus->BroadcastPayload(SimEcoPricingTags::Bus_AuctionChanged, FInstancedStruct(), Instigator);
	}
}

// ----------------------------------------------------------------------------------------------------
// Persistence
// ----------------------------------------------------------------------------------------------------

void USimEco_AuctionSubsystem::CaptureState_Implementation(FInstancedStruct& Out) const
{
	FSimEco_AuctionSaveRecord Record;
	Record.NextLotId = NextLotId;
	for (const FSimEco_AuctionLot& Lot : Lots)
	{
		if (Lot.State != ESimEco_AuctionState::Active)
		{
			continue;
		}
		FSimEco_AuctionSavedLot Saved;
		Saved.LotId = Lot.LotId;
		Saved.ItemTag = Lot.ItemTag;
		Saved.Quantity = Lot.Quantity;
		Saved.CurrencyTag = Lot.CurrencyTag;
		Saved.MinBid = Lot.MinBid;
		Saved.BuyoutPrice = Lot.BuyoutPrice;
		Saved.SettlementDay = Lot.SettlementDay;
		Record.SavedLots.Add(Saved);
	}
	Out.InitializeAs<FSimEco_AuctionSaveRecord>(Record);
}

void USimEco_AuctionSubsystem::RestoreState_Implementation(const FInstancedStruct& In)
{
	// Authority guard: a client-side load must never reconstitute escrowed goods.
	if (!HasWorldAuthority())
	{
		return;
	}
	const FSimEco_AuctionSaveRecord* Record = In.GetPtr<FSimEco_AuctionSaveRecord>();
	if (!Record)
	{
		return;
	}

	Lots.Reset();
	Escrow.Reset();
	NextLotId = FMath::Max(1, Record->NextLotId);

	// Conservative restore: reconstitute item-escrowed lots with NO bid (currency escrow is dropped, so
	// the save can never duplicate currency). Goods remain in escrow (they were already removed from the
	// seller pre-save). The seller actor pointer is unknown post-load until the seller re-registers; the
	// lot still settles by returning goods to a re-resolved seller via its persisted SellerId is out of
	// scope here — on expiry with no resolvable seller the goods are simply reaped (documented).
	for (const FSimEco_AuctionSavedLot& Saved : Record->SavedLots)
	{
		FSimEco_AuctionLot Lot;
		Lot.LotId = Saved.LotId;
		Lot.ItemTag = Saved.ItemTag;
		Lot.Quantity = Saved.Quantity;
		Lot.CurrencyTag = Saved.CurrencyTag;
		Lot.MinBid = Saved.MinBid;
		Lot.CurrentBid = 0;
		Lot.BuyoutPrice = Saved.BuyoutPrice;
		Lot.SettlementDay = Saved.SettlementDay;
		Lot.State = ESimEco_AuctionState::Active;
		Lots.Add(Lot);

		FSimEco_AuctionEscrow E;
		E.ItemTag = Saved.ItemTag;
		E.Quantity = Saved.Quantity;
		E.CurrencyTag = Saved.CurrencyTag;
		Escrow.Add(Saved.LotId, E);

		SyncLotToProxy(Lot);
	}
}

FGameplayTag USimEco_AuctionSubsystem::GetPersistenceKind_Implementation() const
{
	return SimEcoPricingTags::Persist_Auction;
}

FString USimEco_AuctionSubsystem::GetDPDebugString_Implementation() const
{
	return FString::Printf(TEXT("Auction auth=%d lots=%d escrow=%d nextId=%d"),
		HasWorldAuthority() ? 1 : 0, Lots.Num(), Escrow.Num(), NextLotId);
}
