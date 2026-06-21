// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Seam_PriceQuote.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_PriceQuote : public UInterface
{
	GENERATED_BODY()
};

/**
 * Read-only PRICING seam: "what does this thing cost right now?".
 *
 * The simulation economy implements this (a market adapter) and registers it under
 * DP.Service.Eco.PriceQuote. The Progression shop and any UI read live, demand-formed prices through
 * this seam WITHOUT including the SimEconomy module. It is intentionally read-only — actually taking
 * money happens through ISeam_WalletAuthority on the authoritative buy path, never here.
 *
 * GetQuotedPrice returns the base per-unit quote for an item/commodity tag. GetPriceMultiplierForFaction
 * folds in a faction/reputation-driven multiplier so a buyer's standing can discount (or surcharge) the
 * base quote without the pricing provider knowing about the buyer's concrete reputation type.
 */
class DESIGNPATTERNSSEAMS_API ISeam_PriceQuote
{
	GENERATED_BODY()

public:
	/**
	 * Current per-unit base price for ItemOrCommodityTag (0 when the provider does not price it). The
	 * quote is the demand-formed market price BEFORE any per-buyer faction/reputation multiplier.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Economy")
	double GetQuotedPrice(FGameplayTag ItemOrCommodityTag) const;

	/**
	 * The price multiplier to apply for a given faction/buyer (1.0 = no change, <1 = discount, >1 =
	 * surcharge). Buyer may be null (returns the neutral multiplier). The provider may consult an
	 * ISeam_Reputation resolved off Buyer to derive a standing-based discount.
	 *
	 * @param FactionTag  The merchant/faction context the price is being quoted in.
	 * @param Buyer       The actor the price is being quoted for (may be null).
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Economy")
	float GetPriceMultiplierForFaction(FGameplayTag FactionTag, const AActor* Buyer) const;
};
