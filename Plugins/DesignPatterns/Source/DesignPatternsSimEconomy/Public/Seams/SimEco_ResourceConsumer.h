// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "SimEco_ResourceConsumer.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USimEco_ResourceConsumer : public UInterface
{
	GENERATED_BODY()
};

/**
 * Seam implemented by anything that draws down commodities over time without necessarily yielding
 * a tradeable output (population upkeep, a furnace burning fuel, building maintenance). Demand and
 * shortage aggregation reads consumers through TScriptInterface<ISimEco_ResourceConsumer>.
 *
 * BlueprintNativeEvent throughout. Read methods are const and client-safe; control methods are
 * authority-gated by the implementer.
 */
class DESIGNPATTERNSSIMECONOMY_API ISimEco_ResourceConsumer
{
	GENERATED_BODY()

public:
	/** Identity tag of the consumption process currently active (invalid when idle). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|SimEco|Consumer")
	FGameplayTag GetActiveProcessTag() const;

	/** True while actively consuming. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|SimEco|Consumer")
	bool IsConsuming() const;

	/** Normalized progress [0,1] toward the next consumption draw (client-derived). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|SimEco|Consumer")
	float GetConsumptionProgress() const;

	/** What this consumer draws per cycle (commodity tag -> quantity). For demand planning/UI. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|SimEco|Consumer")
	void GetExpectedInputs(TArray<FGameplayTag>& OutCommodities, TArray<float>& OutQuantities) const;

	/**
	 * True if the last cycle could not be fully satisfied from the stockpile (a shortage occurred).
	 * Lets demand systems flag starving consumers without inspecting their internals.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|SimEco|Consumer")
	bool IsStarved() const;

	/** Request this consumer to run ProcessTag. AUTHORITY ONLY at the implementer. Returns acceptance. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|SimEco|Consumer")
	bool SetActiveProcess(FGameplayTag ProcessTag);

	/** Stop consuming, releasing any reserved inputs. AUTHORITY ONLY. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|SimEco|Consumer")
	void CancelConsumption();
};
