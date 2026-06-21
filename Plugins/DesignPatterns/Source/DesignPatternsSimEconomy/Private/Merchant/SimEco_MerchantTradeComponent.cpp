// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Merchant/SimEco_MerchantTradeComponent.h"
#include "Merchant/SimEco_MerchantComponent.h"
#include "Pricing/SimEco_PriceModifierDef.h"
#include "Pricing/SimEco_PricingTags.h"
#include "Economy/Seam_WalletAuthority.h"
#include "Economy/Seam_TradableInventory.h"
#include "Economy/Seam_PurchaseTarget.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"
#include "GameFramework/Actor.h"

namespace
{
	/**
	 * Resolve a UINTERFACE seam object off an actor: the actor itself first, then its components.
	 * Returns the UObject implementing SeamClass, or null. (Same pattern as the shop's resolver.)
	 */
	UObject* ResolveSeam(const AActor* Actor, TSubclassOf<UInterface> SeamClass)
	{
		if (!Actor || !*SeamClass)
		{
			return nullptr;
		}
		if (Actor->GetClass()->ImplementsInterface(SeamClass))
		{
			return const_cast<AActor*>(Actor);
		}
		if (UActorComponent* Comp = const_cast<AActor*>(Actor)->FindComponentByInterface(SeamClass))
		{
			return Comp;
		}
		return nullptr;
	}
}

USimEco_MerchantTradeComponent::USimEco_MerchantTradeComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	// No replicated state — only RPC routing. Replication must be on so the RPCs route for the owner.
	SetIsReplicatedByDefault(true);
}

bool USimEco_MerchantTradeComponent::OwnerHasAuthority() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}

USimEco_MerchantComponent* USimEco_MerchantTradeComponent::ResolveMerchant(AActor* MerchantActor)
{
	return MerchantActor ? MerchantActor->FindComponentByClass<USimEco_MerchantComponent>() : nullptr;
}

FGameplayTag USimEco_MerchantTradeComponent::ResolveCurrencyTag(const USimEco_MerchantComponent* Merchant)
{
	if (!Merchant)
	{
		return FGameplayTag();
	}
	if (Merchant->CurrencyTag.IsValid())
	{
		return Merchant->CurrencyTag;
	}
	return FGameplayTag();
}

// ----------------------------------------------------------------------------------------------------
// Client entry points
// ----------------------------------------------------------------------------------------------------

void USimEco_MerchantTradeComponent::RequestBuy(AActor* Merchant, FGameplayTag ItemTag, int32 Count, float HaggleMultiplier)
{
	if (!Merchant || !ItemTag.IsValid() || Count <= 0)
	{
		OnTradeResolved.Broadcast(ESimEco_MerchantResult::BadRequest, ItemTag, 0);
		return;
	}

	if (OwnerHasAuthority())
	{
		int64 Total = 0;
		const ESimEco_MerchantResult Result = ExecuteBuy(GetOwner(), ResolveMerchant(Merchant), ItemTag, Count, HaggleMultiplier, Total);
		OnTradeResolved.Broadcast(Result, ItemTag, Total);
		return;
	}
	Server_Buy(Merchant, ItemTag, Count, HaggleMultiplier);
}

void USimEco_MerchantTradeComponent::RequestSell(AActor* Merchant, FGameplayTag ItemTag, int32 Count)
{
	if (!Merchant || !ItemTag.IsValid() || Count <= 0)
	{
		OnTradeResolved.Broadcast(ESimEco_MerchantResult::BadRequest, ItemTag, 0);
		return;
	}

	if (OwnerHasAuthority())
	{
		int64 Total = 0;
		const ESimEco_MerchantResult Result = ExecuteSell(GetOwner(), ResolveMerchant(Merchant), ItemTag, Count, Total);
		OnTradeResolved.Broadcast(Result, ItemTag, Total);
		return;
	}
	Server_Sell(Merchant, ItemTag, Count);
}

// ----------------------------------------------------------------------------------------------------
// Server RPCs
// ----------------------------------------------------------------------------------------------------

bool USimEco_MerchantTradeComponent::Server_Buy_Validate(AActor* Merchant, FGameplayTag ItemTag, int32 Count, float HaggleMultiplier)
{
	return Merchant != nullptr && ItemTag.IsValid() && Count > 0 && FMath::IsFinite(HaggleMultiplier);
}

void USimEco_MerchantTradeComponent::Server_Buy_Implementation(AActor* Merchant, FGameplayTag ItemTag, int32 Count, float HaggleMultiplier)
{
	int64 Total = 0;
	const ESimEco_MerchantResult Result = ExecuteBuy(GetOwner(), ResolveMerchant(Merchant), ItemTag, Count, HaggleMultiplier, Total);
	Client_TradeResolved(Result, ItemTag, Total);
}

bool USimEco_MerchantTradeComponent::Server_Sell_Validate(AActor* Merchant, FGameplayTag ItemTag, int32 Count)
{
	return Merchant != nullptr && ItemTag.IsValid() && Count > 0;
}

void USimEco_MerchantTradeComponent::Server_Sell_Implementation(AActor* Merchant, FGameplayTag ItemTag, int32 Count)
{
	int64 Total = 0;
	const ESimEco_MerchantResult Result = ExecuteSell(GetOwner(), ResolveMerchant(Merchant), ItemTag, Count, Total);
	Client_TradeResolved(Result, ItemTag, Total);
}

void USimEco_MerchantTradeComponent::Client_TradeResolved_Implementation(ESimEco_MerchantResult Result, FGameplayTag ItemTag, int64 TotalPrice)
{
	OnTradeResolved.Broadcast(Result, ItemTag, TotalPrice);
}

// ----------------------------------------------------------------------------------------------------
// Authoritative executors (server). Re-derive price; move currency + item atomically.
// ----------------------------------------------------------------------------------------------------

ESimEco_MerchantResult USimEco_MerchantTradeComponent::ExecuteBuy(AActor* Buyer, USimEco_MerchantComponent* Merchant,
	const FGameplayTag& ItemTag, int32 Count, float HaggleMultiplier, int64& OutTotalPrice)
{
	OutTotalPrice = 0;

	if (!OwnerHasAuthority())
	{
		return ESimEco_MerchantResult::NotAuthoritative;
	}
	if (!Buyer || !ItemTag.IsValid() || Count <= 0)
	{
		return ESimEco_MerchantResult::BadRequest;
	}
	if (!Merchant)
	{
		return ESimEco_MerchantResult::NoMerchant;
	}

	// Server clamps the requested count to its hard bound.
	const int32 ClampedCount = FMath::Clamp(Count, 1, FMath::Max(1, MaxTradeCount));

	if (!Merchant->DealsIn(ItemTag))
	{
		return ESimEco_MerchantResult::NotStocked;
	}
	if (Merchant->GetStock(ItemTag) < ClampedCount)
	{
		return ESimEco_MerchantResult::OutOfStock;
	}

	// Re-derive the AUTHORITATIVE per-unit ask price (never trust the client's haggle).
	const int64 UnitPrice = Merchant->QuoteUnitPrice(ItemTag, ESimEco_TradeSide::PlayerBuy, Buyer, HaggleMultiplier);
	if (UnitPrice <= 0)
	{
		return ESimEco_MerchantResult::NotStocked;
	}
	const int64 TotalPrice = UnitPrice * (int64)ClampedCount;

	// Resolve the seams off the buyer.
	const FGameplayTag Currency = ResolveCurrencyTag(Merchant);
	UObject* WalletObj = ResolveSeam(Buyer, USeam_WalletAuthority::StaticClass());
	UObject* InvObj = ResolveSeam(Buyer, USeam_PurchaseTarget::StaticClass());
	if (!WalletObj || !Currency.IsValid())
	{
		return ESimEco_MerchantResult::NoWallet;
	}
	if (!InvObj)
	{
		return ESimEco_MerchantResult::NoInventory;
	}

	// Pre-checks before any mutation (so we don't half-commit).
	if (!ISeam_WalletAuthority::Execute_CanSpend(WalletObj, Currency, TotalPrice))
	{
		return ESimEco_MerchantResult::CannotAfford;
	}
	if (!ISeam_PurchaseTarget::Execute_CanReceive(InvObj, ItemTag, ClampedCount))
	{
		return ESimEco_MerchantResult::NoInventory;
	}

	// --- Atomic commit, in order, with rollback on the delivery step ---
	if (!ISeam_WalletAuthority::Execute_Spend(WalletObj, Currency, TotalPrice))
	{
		return ESimEco_MerchantResult::CannotAfford;
	}

	const int32 Granted = ISeam_PurchaseTarget::Execute_GrantItem(InvObj, ItemTag, ClampedCount);
	if (Granted < ClampedCount)
	{
		// Roll back: refund the full price (best-effort) and undo any partial grant is the inventory's
		// concern; we refund what we took. Stock was not yet decremented.
		ISeam_WalletAuthority::Execute_Grant(WalletObj, Currency, TotalPrice);
		return ESimEco_MerchantResult::TransferFailed;
	}

	// Currency + item moved; now decrement merchant stock (authority).
	Merchant->ConsumeStockForSale(ItemTag, ClampedCount);

	OutTotalPrice = TotalPrice;

	// After-the-fact notification on the bus (never a command).
	if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		Bus->BroadcastPayload(SimEcoPricingTags::Bus_MerchantTrade, FInstancedStruct(), Buyer);
	}
	UE_LOG(LogDP, Verbose, TEXT("[MerchantTrade] BUY %s x%d for %lld"), *ItemTag.ToString(), ClampedCount, TotalPrice);
	return ESimEco_MerchantResult::Success;
}

ESimEco_MerchantResult USimEco_MerchantTradeComponent::ExecuteSell(AActor* Seller, USimEco_MerchantComponent* Merchant,
	const FGameplayTag& ItemTag, int32 Count, int64& OutTotalPrice)
{
	OutTotalPrice = 0;

	if (!OwnerHasAuthority())
	{
		return ESimEco_MerchantResult::NotAuthoritative;
	}
	if (!Seller || !ItemTag.IsValid() || Count <= 0)
	{
		return ESimEco_MerchantResult::BadRequest;
	}
	if (!Merchant)
	{
		return ESimEco_MerchantResult::NoMerchant;
	}

	const int32 ClampedCount = FMath::Clamp(Count, 1, FMath::Max(1, MaxTradeCount));

	if (!Merchant->DealsIn(ItemTag))
	{
		return ESimEco_MerchantResult::NotStocked;
	}

	const FGameplayTag Currency = ResolveCurrencyTag(Merchant);
	UObject* WalletObj = ResolveSeam(Seller, USeam_WalletAuthority::StaticClass());
	UObject* InvObj = ResolveSeam(Seller, USeam_TradableInventory::StaticClass());
	if (!WalletObj || !Currency.IsValid())
	{
		return ESimEco_MerchantResult::NoWallet;
	}
	if (!InvObj)
	{
		return ESimEco_MerchantResult::NoInventory;
	}

	if (!ISeam_TradableInventory::Execute_CanRemove(InvObj, ItemTag, ClampedCount))
	{
		return ESimEco_MerchantResult::CannotRemoveItem;
	}

	// Re-derive the AUTHORITATIVE per-unit bid price.
	const int64 UnitPrice = Merchant->QuoteUnitPrice(ItemTag, ESimEco_TradeSide::PlayerSell, Seller, 1.0f);
	if (UnitPrice <= 0)
	{
		return ESimEco_MerchantResult::NotStocked;
	}

	// --- Atomic commit: remove the item, then pay ---
	const int32 Removed = ISeam_TradableInventory::Execute_RemoveItem(InvObj, ItemTag, ClampedCount);
	if (Removed <= 0)
	{
		return ESimEco_MerchantResult::CannotRemoveItem;
	}
	const int64 TotalPrice = UnitPrice * (int64)Removed;

	const int64 Granted = ISeam_WalletAuthority::Execute_Grant(WalletObj, Currency, TotalPrice);
	if (Granted <= 0)
	{
		// Could not pay: the only honest rollback is to give the item back. The purchase-target seam is
		// the add path; resolve it to restore goods.
		if (UObject* AddObj = ResolveSeam(Seller, USeam_PurchaseTarget::StaticClass()))
		{
			ISeam_PurchaseTarget::Execute_GrantItem(AddObj, ItemTag, Removed);
		}
		return ESimEco_MerchantResult::TransferFailed;
	}

	// Item + currency moved; merchant absorbs the goods into its stock.
	Merchant->AddStockFromPurchase(ItemTag, Removed);

	OutTotalPrice = TotalPrice;

	if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		Bus->BroadcastPayload(SimEcoPricingTags::Bus_MerchantTrade, FInstancedStruct(), Seller);
	}
	UE_LOG(LogDP, Verbose, TEXT("[MerchantTrade] SELL %s x%d for %lld"), *ItemTag.ToString(), Removed, TotalPrice);
	return ESimEco_MerchantResult::Success;
}
