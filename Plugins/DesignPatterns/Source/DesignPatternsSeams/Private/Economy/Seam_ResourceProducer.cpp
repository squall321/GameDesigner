// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Economy/Seam_ResourceProducer.h"

/**
 * Default BlueprintNativeEvent bodies for ISeam_ResourceProducer. Concrete producers (SimEco_ProducerComponent,
 * UBuild_FacilityProducerComponent) override these. The defaults fail-safe: an unimplemented producer is idle
 * with no active process, yields nothing, and all control mutations are rejected. This allows Blueprint actors
 * to partially implement the interface without crashing on unoverridden methods.
 */

FGameplayTag ISeam_ResourceProducer::GetActiveProcessTag_Implementation() const
{
	return FGameplayTag();
}

bool ISeam_ResourceProducer::IsProducing_Implementation() const
{
	return false;
}

float ISeam_ResourceProducer::GetProductionProgress_Implementation() const
{
	return 0.0f;
}

void ISeam_ResourceProducer::GetExpectedOutputs_Implementation(TArray<FGameplayTag>& OutCommodities, TArray<float>& OutQuantities) const
{
	OutCommodities.Reset();
	OutQuantities.Reset();
}

bool ISeam_ResourceProducer::SetActiveProcess_Implementation(FGameplayTag /*ProcessTag*/)
{
	return false;
}

void ISeam_ResourceProducer::CancelProduction_Implementation()
{
}
