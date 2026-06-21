// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Seam_ResourceConsumer.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_ResourceConsumer : public UInterface
{
	GENERATED_BODY()
};

/**
 * Leaf-level seam for "anything that draws down commodities over time" without necessarily yielding
 * a tradeable output (population upkeep, a furnace burning fuel, building maintenance). PROMOTED
 * from DesignPatternsSimEconomy's ISimEco_ResourceConsumer so demand/shortage aggregation can read
 * consumers across genre modules through Seams only.
 *
 * Signatures mirror ISimEco_ResourceConsumer exactly. SimEconomy's USimEco_ConsumerComponent
 * additively also implements this seam. BlueprintNativeEvent throughout; read methods are const and
 * client-safe; control methods are AUTHORITY-gated by the implementer.
 */
class DESIGNPATTERNSSEAMS_API ISeam_ResourceConsumer
{
	GENERATED_BODY()

public:
	/** Identity tag of the consumption process currently active (invalid when idle). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Consumer")
	FGameplayTag GetActiveProcessTag() const;

	/** True while actively consuming. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Consumer")
	bool IsConsuming() const;

	/** Normalized progress [0,1] toward the next consumption draw (client-derived). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Consumer")
	float GetConsumptionProgress() const;

	/** What this consumer draws per cycle (commodity tag -> quantity). For demand planning/UI. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Consumer")
	void GetExpectedInputs(TArray<FGameplayTag>& OutCommodities, TArray<float>& OutQuantities) const;

	/**
	 * True if the last cycle could not be fully satisfied from the source store (a shortage occurred).
	 * Lets demand systems flag starving consumers without inspecting their internals.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Consumer")
	bool IsStarved() const;

	/** Request this consumer to run ProcessTag. AUTHORITY ONLY at the implementer. Returns acceptance. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Consumer")
	bool SetActiveProcess(FGameplayTag ProcessTag);

	/** Stop consuming, releasing any reserved inputs. AUTHORITY ONLY. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Consumer")
	void CancelConsumption();
};
