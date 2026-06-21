// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "SimEco_MerchantTradeComponent.generated.h"

class USimEco_MerchantComponent;

/** Why a merchant buy/sell failed (or succeeded). Mirrored to the requesting client for precise UI. */
UENUM(BlueprintType)
enum class ESimEco_MerchantResult : uint8
{
	/** Trade completed: currency moved, item moved, stock updated. */
	Success,
	/** The named merchant actor exposed no USimEco_MerchantComponent. */
	NoMerchant,
	/** The merchant does not deal in the requested item. */
	NotStocked,
	/** A player-buy where the merchant has insufficient stock. */
	OutOfStock,
	/** The buyer exposes no wallet authority seam, so currency could not move. */
	NoWallet,
	/** The buyer cannot afford the (server-recomputed) price. */
	CannotAfford,
	/** The buyer exposes no purchase target, so a bought item could not be delivered. */
	NoInventory,
	/** A player-sell where the buyer cannot remove the item (not held / soulbound). */
	CannotRemoveItem,
	/** The item-delivery/removal step rejected the transaction (rolled back). */
	TransferFailed,
	/** Called off-authority (defensive; the server re-checks everything). */
	NotAuthoritative,
	/** Malformed request (bad tags / counts). */
	BadRequest
};

/** Broadcast on the owning client once the server resolves a trade this component requested. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FSimEco_OnMerchantTradeResolved,
	ESimEco_MerchantResult, Result, FGameplayTag, ItemTag, int64, TotalPrice);

/**
 * PLAYER-OWNED component that routes a player's merchant buy/sell INTENT to the authoritative server.
 *
 * This is THE single authoritative live-priced trade path (resolving the Progression-shop "double
 * charge" risk): the server RE-DERIVES the effective price from the merchant's live market price +
 * pricing rules, NEVER trusting a client-quoted or client-haggled number, then performs the transfer
 * ATOMICALLY:
 *   Player BUY:  recompute ask price -> ISeam_WalletAuthority::Spend -> merchant.ConsumeStockForSale ->
 *                ISeam_PurchaseTarget::GrantItem; any failure rolls back the prior step.
 *   Player SELL: verify ISeam_TradableInventory::CanRemove -> recompute bid price -> RemoveItem ->
 *                ISeam_WalletAuthority::Grant -> merchant.AddStockFromPurchase.
 *
 * Cross-module coupling is ONLY through seams (wallet authority / purchase target / tradable inventory)
 * resolved off the buyer actor — the economy never includes the wallet or inventory module. The
 * merchant pricing component is a SIBLING in this module (concrete include permitted). Haggle is a
 * proposed multiplier the SERVER clamps to the merchant's authored floor.
 *
 * Put this on a player-owned actor (PlayerController / Pawn / PlayerState) so the RPCs have an owning
 * connection.
 */
UCLASS(ClassGroup = (DesignPatternsSimEconomy), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSSIMECONOMY_API USimEco_MerchantTradeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USimEco_MerchantTradeComponent();

	/**
	 * Client entry point: buy Count of ItemTag from Merchant, proposing HaggleMultiplier (the server
	 * clamps it). On a listen/standalone host this executes directly; on a client it forwards a
	 * validated Server RPC. The buyer is this component's owning actor.
	 */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Merchant")
	void RequestBuy(AActor* Merchant, FGameplayTag ItemTag, int32 Count, float HaggleMultiplier = 1.0f);

	/** Client entry point: sell Count of ItemTag to Merchant. */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Merchant")
	void RequestSell(AActor* Merchant, FGameplayTag ItemTag, int32 Count);

	/** Fired on the owning client once the server resolves a requested trade. */
	UPROPERTY(BlueprintAssignable, Category = "SimEconomy|Merchant")
	FSimEco_OnMerchantTradeResolved OnTradeResolved;

	/**
	 * Server-side cap on a single trade's unit count, so a compromised client cannot request an absurd
	 * quantity. A tunable, not a magic number.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimEconomy|Merchant",
		meta = (ClampMin = "1", UIMin = "1"))
	int32 MaxTradeCount = 999;

	/**
	 * AUTHORITY-ONLY core trade executor (server). Re-derives price, moves currency + item atomically,
	 * updates merchant stock. Public so a server-side system (e.g. an NPC-to-merchant flow) can reuse
	 * it directly. Returns the outcome and writes the total price paid/received to OutTotalPrice.
	 */
	ESimEco_MerchantResult ExecuteBuy(AActor* Buyer, USimEco_MerchantComponent* Merchant,
		const FGameplayTag& ItemTag, int32 Count, float HaggleMultiplier, int64& OutTotalPrice);

	ESimEco_MerchantResult ExecuteSell(AActor* Seller, USimEco_MerchantComponent* Merchant,
		const FGameplayTag& ItemTag, int32 Count, int64& OutTotalPrice);

private:
	/** Server RPC carrying a buy intent. WithValidation rejects malformed input before execution. */
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_Buy(AActor* Merchant, FGameplayTag ItemTag, int32 Count, float HaggleMultiplier);

	/** Server RPC carrying a sell intent. */
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_Sell(AActor* Merchant, FGameplayTag ItemTag, int32 Count);

	/** Client RPC delivering the resolved outcome back to the requesting client. */
	UFUNCTION(Client, Reliable)
	void Client_TradeResolved(ESimEco_MerchantResult Result, FGameplayTag ItemTag, int64 TotalPrice);

	/** True if the owning actor has network authority. */
	bool OwnerHasAuthority() const;

	/** Resolve the merchant pricing/stock component off a merchant actor (sibling type; may be null). */
	static USimEco_MerchantComponent* ResolveMerchant(AActor* MerchantActor);

	/** Resolve the wallet currency tag a merchant denominates in (component tag, else modifier asset). */
	static FGameplayTag ResolveCurrencyTag(const USimEco_MerchantComponent* Merchant);
};
