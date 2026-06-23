// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Economy/Seam_ResourceConsumer.h"

/**
 * Default BlueprintNativeEvent implementations for ISeam_ResourceConsumer. The seam carries no behaviour
 * of its own — economy consumer components override these methods. These fail-closed defaults ensure that
 * an unimplemented consumer does not draw resources, allows starved detection, and safely rejects process changes.
 */

FGameplayTag ISeam_ResourceConsumer::GetActiveProcessTag_Implementation() const
{
	return FGameplayTag();
}

bool ISeam_ResourceConsumer::IsConsuming_Implementation() const
{
	return false;
}

float ISeam_ResourceConsumer::GetConsumptionProgress_Implementation() const
{
	return 0.0f;
}

void ISeam_ResourceConsumer::GetExpectedInputs_Implementation(TArray<FGameplayTag>& OutCommodities, TArray<float>& OutQuantities) const
{
	OutCommodities.Reset();
	OutQuantities.Reset();
}

bool ISeam_ResourceConsumer::IsStarved_Implementation() const
{
	return false;
}

bool ISeam_ResourceConsumer::SetActiveProcess_Implementation(FGameplayTag /*ProcessTag*/)
{
	return false;
}

void ISeam_ResourceConsumer::CancelConsumption_Implementation()
{
}
