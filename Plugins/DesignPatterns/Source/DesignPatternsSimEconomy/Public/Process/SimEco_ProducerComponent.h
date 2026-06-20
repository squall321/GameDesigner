// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Process/SimEco_ProcessComponentBase.h"
#include "Seams/SimEco_ResourceProducer.h"
#include "SimEco_ProducerComponent.generated.h"

/**
 * Authority-driven producer: runs a USimEco_ProcessDef recipe in repeating cycles, reserving its
 * inputs at cycle start and, on completion, committing those inputs and depositing the outputs into
 * the target stockpile. Implements ISimEco_ResourceProducer so markets/aggregators read it through
 * the seam.
 *
 * Only (process tag, server cycle start, cycle length) replicate (from the base); clients derive
 * progress and never mutate state. On cancel, reserved-but-uncommitted inputs are released.
 */
UCLASS(ClassGroup = (DesignPatternsSimEconomy), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSSIMECONOMY_API USimEco_ProducerComponent
	: public USimEco_ProcessComponentBase
	, public ISimEco_ResourceProducer
{
	GENERATED_BODY()

public:
	//~ Begin ISimEco_ResourceProducer
	virtual FGameplayTag GetActiveProcessTag_Implementation() const override { return GetProcessTag(); }
	virtual bool IsProducing_Implementation() const override { return IsRunning(); }
	virtual float GetProductionProgress_Implementation() const override { return GetCycleProgress(); }
	virtual void GetExpectedOutputs_Implementation(TArray<FGameplayTag>& OutCommodities, TArray<float>& OutQuantities) const override;
	virtual bool SetActiveProcess_Implementation(FGameplayTag ProcessTag) override { return StartProcess(ProcessTag); }
	virtual void CancelProduction_Implementation() override { CancelProcess(); }
	//~ End ISimEco_ResourceProducer

protected:
	//~ Begin USimEco_ProcessComponentBase
	virtual bool OnCycleBegun(const USimEco_ProcessDef& Def, USimEco_StockpileComponent& Stockpile) override;
	virtual void OnCycleCompleted(const USimEco_ProcessDef& Def, USimEco_StockpileComponent& Stockpile) override;
	virtual void OnProcessCancelled(const USimEco_ProcessDef& Def, USimEco_StockpileComponent& Stockpile) override;
	//~ End USimEco_ProcessComponentBase
};
