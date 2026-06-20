// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "SimEco_ResourceProducer.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USimEco_ResourceProducer : public UInterface
{
	GENERATED_BODY()
};

/**
 * Seam implemented by anything that turns inputs into outputs over time (a workshop, a farm plot,
 * a refinery). Markets and aggregation systems read producers through TScriptInterface<ISimEco_ResourceProducer>
 * so they never depend on the concrete USimEco_ProducerComponent.
 *
 * All methods are BlueprintNativeEvent so a Blueprint actor can also present as a producer. Read
 * methods are const and safe on clients; control methods (Set/Cancel) are authority-gated by the
 * implementer and no-op on clients.
 */
class DESIGNPATTERNSSIMECONOMY_API ISimEco_ResourceProducer
{
	GENERATED_BODY()

public:
	/** Identity tag of the process this producer is currently running (invalid when idle). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|SimEco|Producer")
	FGameplayTag GetActiveProcessTag() const;

	/** True while a production cycle is in progress (inputs reserved, outputs pending). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|SimEco|Producer")
	bool IsProducing() const;

	/**
	 * Normalized progress [0,1] through the current cycle. Derived locally from the replicated
	 * server start time and cycle length, so it is smooth and correct on clients without per-frame
	 * replication.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|SimEco|Producer")
	float GetProductionProgress() const;

	/** What this producer yields per completed cycle (commodity tag -> quantity). For UI/planning. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|SimEco|Producer")
	void GetExpectedOutputs(TArray<FGameplayTag>& OutCommodities, TArray<float>& OutQuantities) const;

	/**
	 * Request this producer to run ProcessTag. AUTHORITY ONLY at the implementer; ignored on clients
	 * (route client intent through a player-owned component). Returns true if accepted.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|SimEco|Producer")
	bool SetActiveProcess(FGameplayTag ProcessTag);

	/**
	 * Abort the current cycle, releasing any reserved inputs back to the stockpile. AUTHORITY ONLY.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|SimEco|Producer")
	void CancelProduction();
};
