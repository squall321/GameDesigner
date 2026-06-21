// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Economy/Seam_RewardSink.h"

/**
 * Default BlueprintNativeEvent body for ISeam_RewardSink. The seam carries no behaviour of its own —
 * the economy's reward component overrides PayReward. This fail-closed default never pays out.
 */
bool ISeam_RewardSink::PayReward_Implementation(AActor* /*Receiver*/, const FSeam_RewardSpec& /*Spec*/)
{
	return false;
}
