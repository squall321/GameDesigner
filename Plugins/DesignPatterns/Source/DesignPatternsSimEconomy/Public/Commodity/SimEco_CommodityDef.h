// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "SimEco_CommodityDef.generated.h"

/**
 * Tag-keyed definition of a tradeable commodity (grain, planks, iron ore...).
 *
 * Stockpiles, processes and markets all reference a commodity by its DataTag (inherited from
 * UDP_DataAsset) — never by a hard pointer — so the catalog can change without invalidating
 * saved or replicated economy state. Resolve the definition (for display / base value /
 * divisibility) from the core data registry via the DataTag.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSSIMECONOMY_API USimEco_CommodityDef : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	USimEco_CommodityDef();

	//~ Begin UDP_DataAsset
	/** Collapses every commodity into one asset-manager bucket ("SimEco.Commodity"). */
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset

	/** Short label shown in trade/stockpile UIs and debug output. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimEconomy|Commodity")
	FText DisplayLabel;

	/**
	 * Reference "intrinsic" value of one unit, in the abstract currency. Markets seed and anchor
	 * clearing prices around this; it is a tuning baseline, not a fixed price.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimEconomy|Commodity",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	double BaseValue = 1.0;

	/**
	 * Free-form classification tags (e.g. DP.SimEco.Category.Food, DP.SimEco.Category.Perishable).
	 * Lets rules act on whole groups ("tax all luxuries") without enumerating each commodity.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimEconomy|Commodity")
	FGameplayTagContainer CategoryTags;

	/**
	 * When true the commodity is continuous (oil, water) and quantities may be fractional; when
	 * false it is counted in whole units (ingots, crates) and stockpiles round toward zero on store.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimEconomy|Commodity")
	bool bDivisible = true;

	/**
	 * Quantize a raw quantity according to bDivisible: pass-through when divisible, truncated to a
	 * whole non-negative unit count otherwise. Pure helper used by stockpiles/processes.
	 */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Commodity")
	double Quantize(double RawQuantity) const;

#if WITH_EDITOR
	//~ Begin UObject
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
	//~ End UObject
#endif
};
