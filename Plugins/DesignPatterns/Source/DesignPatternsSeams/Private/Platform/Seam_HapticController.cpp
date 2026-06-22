// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Platform/Seam_HapticController.h"

/**
 * Default BlueprintNativeEvent bodies for ISeam_HapticController. The seam carries no behaviour of
 * its own — the Platform haptic subsystem overrides these. These fail-closed defaults never play and
 * report disabled, so an unimplemented seam can be called safely.
 */

void ISeam_HapticController::PlayHapticByTag_Implementation(FGameplayTag /*EffectTag*/, float /*Scale*/)
{
	// No-op default: nothing plays unless a real provider overrides this.
}

void ISeam_HapticController::StopHaptics_Implementation()
{
	// No-op default.
}

bool ISeam_HapticController::AreHapticsEnabled_Implementation() const
{
	// Fail-closed: an unimplemented controller reports haptics unavailable.
	return false;
}
