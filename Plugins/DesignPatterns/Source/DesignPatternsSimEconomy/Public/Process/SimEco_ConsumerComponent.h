// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Process/SimEco_ProcessComponentBase.h"
#include "Seams/SimEco_ResourceConsumer.h"
#include "SimEco_ConsumerComponent.generated.h"

/**
 * Authority-driven consumer: runs a USimEco_ProcessDef recipe purely to draw down its inputs (and,
 * if the recipe has outputs, also deposit them — e.g. a furnace that consumes fuel + ore and yields
 * ingots). Implements ISimEco_ResourceConsumer for demand/shortage aggregation.
 *
 * Tracks a replicated bStarved flag, set when a cycle cannot reserve its inputs, so clients can show
 * a "no fuel" indicator. Only authority mutates it.
 */
UCLASS(ClassGroup = (DesignPatternsSimEconomy), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSSIMECONOMY_API USimEco_ConsumerComponent
	: public USimEco_ProcessComponentBase
	, public ISimEco_ResourceConsumer
{
	GENERATED_BODY()

public:
	//~ Begin UActorComponent
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent

	//~ Begin ISimEco_ResourceConsumer
	virtual FGameplayTag GetActiveProcessTag_Implementation() const override { return GetProcessTag(); }
	virtual bool IsConsuming_Implementation() const override { return IsRunning(); }
	virtual float GetConsumptionProgress_Implementation() const override { return GetCycleProgress(); }
	virtual void GetExpectedInputs_Implementation(TArray<FGameplayTag>& OutCommodities, TArray<float>& OutQuantities) const override;
	virtual bool IsStarved_Implementation() const override { return bStarved; }
	virtual bool SetActiveProcess_Implementation(FGameplayTag ProcessTag) override { return StartProcess(ProcessTag); }
	virtual void CancelConsumption_Implementation() override { CancelProcess(); }
	//~ End ISimEco_ResourceConsumer

protected:
	//~ Begin USimEco_ProcessComponentBase
	virtual bool OnCycleBegun(const USimEco_ProcessDef& Def, USimEco_StockpileComponent& Stockpile) override;
	virtual void OnCycleCompleted(const USimEco_ProcessDef& Def, USimEco_StockpileComponent& Stockpile) override;
	virtual void OnProcessCancelled(const USimEco_ProcessDef& Def, USimEco_StockpileComponent& Stockpile) override;
	//~ End USimEco_ProcessComponentBase

	/** OnRep hook for the starvation flag (clients react to shortage onset/relief). */
	UFUNCTION()
	void OnRep_Starved();

	/** Set the replicated starvation flag (authority-only; dirties only on change). */
	void SetStarved(bool bNewStarved);

	/** Replicated: true when the last cycle could not reserve its full inputs. */
	UPROPERTY(ReplicatedUsing = OnRep_Starved)
	bool bStarved = false;
};
