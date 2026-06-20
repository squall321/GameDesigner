// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "Curves/CurveFloat.h"
#include "SimEco_MarketSettingsDef.generated.h"

/**
 * Per-commodity price-formation parameters for a market.
 *
 * Every value here is a designer tunable (no magic numbers in code): a commodity's price floor,
 * ceiling, the demand elasticity that maps an order-book supply/demand imbalance into a price move,
 * and an optional base-price curve keyed by the day index so seasonal/scripted baselines can be
 * authored. The commodity is referenced by tag, never a hard pointer.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMECONOMY_API FSimEco_CommodityPriceRule
{
	GENERATED_BODY()

	/** Commodity this rule governs (child of SimEco.Commodity / a USimEco_CommodityDef DataTag). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimEconomy|Market", meta = (Categories = "SimEco.Commodity"))
	FGameplayTag CommodityTag;

	/**
	 * Anchor price used when the day-curve is unset, and the value the clearing price reverts toward
	 * when supply and demand balance. Must lie within [PriceFloor, PriceCeiling].
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimEconomy|Market",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	double BasePrice = 1.0;

	/** Hard lower bound on the clearing price (never sells below cost-of-existence). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimEconomy|Market",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	double PriceFloor = 0.1;

	/** Hard upper bound on the clearing price (scarcity cap). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimEconomy|Market",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	double PriceCeiling = 100.0;

	/**
	 * Demand elasticity: the fraction by which price moves per unit of normalized excess-demand
	 * imbalance per clearing. Higher = twitchier prices. 0 freezes price at BasePrice.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimEconomy|Market",
		meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "2.0"))
	double Elasticity = 0.25;

	/**
	 * Per-clearing fraction by which the price relaxes back toward BasePrice when the book is empty
	 * or balanced, preventing prices from sticking at the floor/ceiling forever. 0 disables reversion.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimEconomy|Market",
		meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	double MeanReversion = 0.05;

	/**
	 * Optional baseline curve evaluated at the simulation day index (X = day number) to produce the
	 * BasePrice for that day. When set it OVERRIDES BasePrice, enabling seasonal/scripted baselines.
	 * Leave unset to use the flat BasePrice.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimEconomy|Market")
	TObjectPtr<UCurveFloat> BasePriceByDayCurve = nullptr;

	bool IsValidRule() const { return CommodityTag.IsValid() && PriceCeiling >= PriceFloor; }
};

/**
 * Tag-keyed market-rules data asset: the complete set of per-commodity price rules a market applies,
 * plus the imbalance-normalization scale shared across commodities. Referenced by markets via a
 * TSoftObjectPtr (see USimEco_DeveloperSettings::DefaultMarketSettings) so content never force-loads
 * at startup.
 *
 * (File name SimEco_MarketSettingsDef per module layout; class is USimEco_MarketSettings to match the
 * settings reference.)
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSSIMECONOMY_API USimEco_MarketSettings : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	USimEco_MarketSettings();

	//~ Begin UDP_DataAsset
	/** Groups every market-settings asset into one asset-manager bucket. */
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset

	/** One price rule per commodity this market trades. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimEconomy|Market", meta = (TitleProperty = "CommodityTag"))
	TArray<FSimEco_CommodityPriceRule> CommodityRules;

	/**
	 * The order-book quantity that counts as a "full unit" of imbalance when normalizing
	 * (supply-demand). Larger = a market that needs bigger order flow to move price. Must be > 0.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimEconomy|Market",
		meta = (ClampMin = "0.01", UIMin = "1.0"))
	double ImbalanceNormalization = 100.0;

	/** Find the rule for CommodityTag; returns nullptr if this market does not trade it. */
	const FSimEco_CommodityPriceRule* FindRule(const FGameplayTag& CommodityTag) const;

	/**
	 * Resolve the effective base price for CommodityTag on DayNumber: evaluates the per-day curve if
	 * present, otherwise the flat BasePrice, clamped into [floor, ceiling]. Returns 0 if no rule.
	 */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Market")
	double GetBasePriceForDay(FGameplayTag CommodityTag, int32 DayNumber) const;

#if WITH_EDITOR
	//~ Begin UObject
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
	//~ End UObject
#endif
};
