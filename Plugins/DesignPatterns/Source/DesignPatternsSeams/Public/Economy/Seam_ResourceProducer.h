// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Seam_ResourceProducer.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_ResourceProducer : public UInterface
{
	GENERATED_BODY()
};

/**
 * Leaf-level seam for "anything that turns inputs into outputs over time" (a workshop, a farm plot,
 * a refinery, a placed survival facility). PROMOTED from DesignPatternsSimEconomy's
 * ISimEco_ResourceProducer so producers/consumers can compose ACROSS genre modules without any of
 * them depending on the concrete SimEconomy producer component.
 *
 * The signatures mirror ISimEco_ResourceProducer exactly (GetActiveProcessTag / IsProducing /
 * GetProductionProgress / GetExpectedOutputs / SetActiveProcess / CancelProduction). SimEconomy's
 * USimEco_ProducerComponent additively ALSO implements this seam (keeping its original interface),
 * and DesignPatternsSurvival's UBuild_FacilityProducerComponent implements it — so the automation
 * chain (markets, aggregators, building output) crosses modules through Seams only.
 *
 * All methods are BlueprintNativeEvent so a Blueprint actor can also present as a producer. Read
 * methods are const and safe on clients (they derive from replicated state); control methods
 * (SetActiveProcess / CancelProduction) are AUTHORITY-gated by the implementer and no-op on clients.
 */
class DESIGNPATTERNSSEAMS_API ISeam_ResourceProducer
{
	GENERATED_BODY()

public:
	/** Identity tag of the process this producer is currently running (invalid when idle). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Producer")
	FGameplayTag GetActiveProcessTag() const;

	/** True while a production cycle is in progress (inputs reserved, outputs pending). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Producer")
	bool IsProducing() const;

	/**
	 * Normalized progress [0,1] through the current cycle. Derived locally from the replicated
	 * server cycle-start time and cycle length, so it is smooth and correct on clients without
	 * per-frame replication.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Producer")
	float GetProductionProgress() const;

	/** What this producer yields per completed cycle (commodity tag -> quantity). For UI/planning. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Producer")
	void GetExpectedOutputs(TArray<FGameplayTag>& OutCommodities, TArray<float>& OutQuantities) const;

	/**
	 * Request this producer to run ProcessTag. AUTHORITY ONLY at the implementer; ignored on clients
	 * (route client intent through a player-owned component). Returns true if accepted.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Producer")
	bool SetActiveProcess(FGameplayTag ProcessTag);

	/** Abort the current cycle, releasing any reserved inputs. AUTHORITY ONLY. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Producer")
	void CancelProduction();
};
