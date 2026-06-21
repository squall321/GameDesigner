// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Shop/Prog_ShopComponent.h"   // FProg_ShopOffer (sibling area, same module)
#include "GameplayTagContainer.h"
#include "Prog_ShopPricingDecorator.generated.h"

class UProg_ShopComponent;

/** Broadcast after re-priced offers are recomputed (UI binds to refresh). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FProg_OnPricedOffersChanged, UProg_ShopComponent*, Shop);

/**
 * DECORATOR (not a subclass — UProg_ShopComponent::GetOffers is non-virtual) that re-prices a shop's
 * static catalogue using the LIVE simulation-economy price quote and the buyer's reputation, FOR UI.
 *
 * It composes the shop component (a sibling on the same vendor actor), calls its public GetOffers, then
 * folds each offer's price through ISeam_PriceQuote (the SimEconomy market adapter, registered under
 * DP.Service.Eco.PriceQuote) and ISeam_PriceQuote::GetPriceMultiplierForFaction (reputation-driven).
 * The economy is reached ONLY through Seams interfaces resolved from the service locator — Progression
 * never includes the SimEconomy module (Build.cs unchanged).
 *
 * IMPORTANT: this decorator is DISPLAY-ONLY. Authoritative buys must NOT go through the shop's currency
 * path with a decorator price (that would be unvalidated). A live-priced purchase is the SimEconomy
 * USimEco_MerchantTradeComponent's job (the single authoritative live-priced path). When no price-quote
 * service is present the decorator returns the shop's static prices unchanged (graceful fallback).
 */
UCLASS(ClassGroup = (DesignPatternsProgression), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSPROGRESSION_API UProg_ShopPricingDecorator : public UActorComponent
{
	GENERATED_BODY()

public:
	UProg_ShopPricingDecorator();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	//~ End UActorComponent

	/** The shop component this decorator re-prices. Auto-resolved off the owner in BeginPlay if unset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Progression|Shop")
	TObjectPtr<UProg_ShopComponent> Shop = nullptr;

	/** The faction context the price quote is requested in (a merchant/region faction). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Progression|Shop")
	FGameplayTag FactionTag;

	/**
	 * When true, an offer whose item tag is priced by the live economy uses the live quote as the base;
	 * otherwise the shop's authored static price is the base. Either way the reputation multiplier is
	 * applied. Lets a project mix economy-priced and fixed-price entries in one shop.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Progression|Shop")
	bool bPreferLiveQuote = true;

	/**
	 * Project the shop's offers with LIVE re-pricing folded in, for Buyer. Safe on clients (read-only).
	 * Falls back to static prices when no price-quote service is available.
	 *
	 * @param Buyer       The actor the prices are quoted for (reputation source); may be null.
	 * @param OutOffers   The re-priced offers (a copy of the shop's offers with Price overwritten).
	 */
	UFUNCTION(BlueprintCallable, Category = "Progression|Shop")
	void GetPricedOffers(const AActor* Buyer, TArray<FProg_ShopOffer>& OutOffers) const;

	/**
	 * Compute the single live, reputation-adjusted unit price for one offer for Buyer. Returns the
	 * offer's static price unchanged when the economy does not price the item / no service is present.
	 */
	UFUNCTION(BlueprintCallable, Category = "Progression|Shop")
	int64 GetLivePriceForOffer(const AActor* Buyer, const FProg_ShopOffer& Offer) const;

	/** Fired after a re-price recompute (call RecomputeAndBroadcast on a price-change bus event). */
	UPROPERTY(BlueprintAssignable, Category = "Progression|Shop")
	FProg_OnPricedOffersChanged OnPricedOffersChanged;

	/** Recompute (implicitly, via the next GetPricedOffers) and fire the change delegate for UI. */
	UFUNCTION(BlueprintCallable, Category = "Progression|Shop")
	void NotifyPricesMayHaveChanged();

private:
	/** Resolve the ISeam_PriceQuote provider from the service locator (may be null). */
	UObject* ResolvePriceQuoteProvider() const;
};
