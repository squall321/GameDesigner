// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "Curves/CurveFloat.h"
#include "SimEco_PriceModifierDef.generated.h"

/**
 * The two sides of a quoted transaction. A merchant SELLS to the player at a marked-up ask price and
 * BUYS from the player at a marked-down bid price; the gap between them is the spread (the merchant's
 * margin). UI and the trade flow both compute the effective price per side through this discriminator.
 */
UENUM(BlueprintType)
enum class ESimEco_TradeSide : uint8
{
	/** The player buys from the merchant (merchant sells). The player pays the ask price. */
	PlayerBuy,
	/** The player sells to the merchant (merchant buys). The player receives the bid price. */
	PlayerSell
};

/**
 * Tag-keyed PRICING-MODIFIER data asset: the complete, designer-authored rule set that turns a market's
 * base clearing price into the EFFECTIVE price a specific merchant quotes to a specific buyer.
 *
 * Every value here is a tunable (no magic numbers in code). The pipeline a quote runs through:
 *   base = market clearing price (or commodity base)
 *   * RegionalMultiplier                          (where you are matters)
 *   * (player-buy: 1 + BaseMarkupFraction; player-sell: 1 - BaseMarkdownFraction)   (the spread)
 *   * ScarcityToMarkupCurve(scarcity ratio)       (rarer => dearer)
 *   * ReputationDiscountCurve(reputation)         (standing => discount on buy, bonus on sell)
 *   * HaggleMultiplier (clamped to [MinHaggleMultiplier, 1])                          (negotiation)
 * then clamped to the merchant's [PriceFloorFraction, PriceCeilingFraction] of base.
 *
 * A merchant component references one of these. Several merchants can share one asset (a "guild price
 * sheet") or each can author its own.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSSIMECONOMY_API USimEco_PriceModifierDef : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	USimEco_PriceModifierDef();

	//~ Begin UDP_DataAsset
	/** Groups every price-modifier asset into one asset-manager bucket. */
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset

	// ---- Spread (merchant margin) ----

	/**
	 * Fraction ADDED to the base price when the player buys (the ask markup). 0.2 => the player pays
	 * 120% of base. Together with BaseMarkdownFraction this forms the merchant's spread.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimEconomy|Pricing|Spread",
		meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "2.0"))
	float BaseMarkupFraction = 0.20f;

	/**
	 * Fraction SUBTRACTED from the base price when the player sells (the bid markdown). 0.4 => the
	 * player receives 60% of base. Must be < 1.0 (a merchant never pays more than base for resale).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimEconomy|Pricing|Spread",
		meta = (ClampMin = "0.0", ClampMax = "0.99", UIMin = "0.0", UIMax = "0.95"))
	float BaseMarkdownFraction = 0.40f;

	// ---- Regional / contextual ----

	/**
	 * Flat multiplier applied to every quote from this merchant — the "where you are" knob. A frontier
	 * outpost might be 1.5 (everything dearer); a capital hub 0.9. 1.0 = no regional effect.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimEconomy|Pricing|Regional",
		meta = (ClampMin = "0.0", UIMin = "0.1", UIMax = "5.0"))
	float RegionalMultiplier = 1.0f;

	// ---- Scarcity (demand elasticity at the merchant level) ----

	/**
	 * Maps a scarcity ratio (desired / available stock, clamped to a sane domain) to a price-markup
	 * multiplier. X = scarcity ratio (1 = exactly enough; >1 = short), Y = multiplier (>=1 when short).
	 * Unset => no scarcity effect (multiplier 1.0). Lets designers author a non-linear "panic-buy" curve.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimEconomy|Pricing|Scarcity")
	TObjectPtr<UCurveFloat> ScarcityToMarkupCurve = nullptr;

	/** Upper clamp on the scarcity ratio fed into the curve, so a near-zero stock can't blow up price. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimEconomy|Pricing|Scarcity",
		meta = (ClampMin = "1.0", UIMin = "1.0", UIMax = "20.0"))
	float MaxScarcityRatio = 5.0f;

	// ---- Reputation ----

	/** Faction context this merchant prices in; reputation discount reads the buyer's standing here. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimEconomy|Pricing|Reputation")
	FGameplayTag MerchantFactionTag;

	/**
	 * Maps the buyer's numeric reputation with MerchantFactionTag to a discount multiplier on a
	 * PLAYER-BUY quote. X = reputation, Y = multiplier (<1 = discount). Unset => no discount (1.0).
	 * A symmetric bonus on player-SELL is derived as (2 - multiplier) clamped to [1, MaxSellRepBonus].
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimEconomy|Pricing|Reputation")
	TObjectPtr<UCurveFloat> ReputationDiscountCurve = nullptr;

	/** Upper clamp on the reputation-derived sell bonus, so high standing can't make sells exceed base. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimEconomy|Pricing|Reputation",
		meta = (ClampMin = "1.0", UIMin = "1.0", UIMax = "2.0"))
	float MaxSellRepBonus = 1.25f;

	// ---- Haggling ----

	/**
	 * Floor for the haggle multiplier a successful negotiation may reach on a player-buy quote. 0.85 =>
	 * the best a player can haggle is 85% of the otherwise-effective price. The server clamps any
	 * client-proposed haggle to [MinHaggleMultiplier, 1] so a compromised client can't set price to 0.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimEconomy|Pricing|Haggle",
		meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.5", UIMax = "1.0"))
	float MinHaggleMultiplier = 0.85f;

	// ---- Final clamp ----

	/** Hard lower bound on the effective price as a fraction of base (never sell below cost). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimEconomy|Pricing|Clamp",
		meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "1.0"))
	float PriceFloorFraction = 0.10f;

	/** Hard upper bound on the effective price as a fraction of base (gouging cap). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimEconomy|Pricing|Clamp",
		meta = (ClampMin = "1.0", UIMin = "1.0", UIMax = "20.0"))
	float PriceCeilingFraction = 5.0f;

	// ---- Pure evaluation helpers (no side effects; safe on clients for UI) ----

	/**
	 * Compute the reputation discount multiplier for a buy quote given a numeric reputation value.
	 * Returns 1.0 when no curve is set. Pure.
	 */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Pricing")
	float EvaluateReputationMultiplier(float Reputation, ESimEco_TradeSide Side) const;

	/**
	 * Compute the scarcity markup multiplier given a scarcity ratio (desired/available). Clamps the
	 * input to [0, MaxScarcityRatio]. Returns 1.0 when no curve is set. Pure.
	 */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Pricing")
	float EvaluateScarcityMultiplier(float ScarcityRatio) const;

	/**
	 * Fold every modifier into one effective per-unit price.
	 *
	 * @param BasePrice       Market base/clearing price (>= 0).
	 * @param Side            Player buy (ask) or sell (bid).
	 * @param Reputation      Buyer reputation with MerchantFactionTag (pass 0 for neutral/unknown).
	 * @param ScarcityRatio   desired/available (pass 1 when scarcity is irrelevant).
	 * @param HaggleMultiplier  Negotiation factor in [MinHaggleMultiplier, 1] for buy (clamped here).
	 * @return The clamped effective per-unit price.
	 */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Pricing")
	double ComputeEffectivePrice(double BasePrice, ESimEco_TradeSide Side, float Reputation,
		float ScarcityRatio, float HaggleMultiplier) const;

#if WITH_EDITOR
	//~ Begin UObject
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
	//~ End UObject
#endif
};
