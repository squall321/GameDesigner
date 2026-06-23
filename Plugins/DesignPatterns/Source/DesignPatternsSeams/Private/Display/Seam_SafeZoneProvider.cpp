// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Display/Seam_SafeZoneProvider.h"

/**
 * Default BlueprintNativeEvent bodies for ISeam_SafeZoneProvider. The Platform display subsystem overrides
 * these with real display metrics. These inert defaults report no safe-zone insets, an unscaled DPI of 1.0,
 * and a zero resolution, so UI/camera consumers fall back to the full viewport when no provider is bound.
 */

FVector4 ISeam_SafeZoneProvider::GetSafeInsets_Implementation() const
{
	return FVector4::Zero();
}

float ISeam_SafeZoneProvider::GetDPIScale_Implementation() const
{
	return 1.0f;
}

FIntPoint ISeam_SafeZoneProvider::GetResolution_Implementation() const
{
	return FIntPoint::ZeroValue;
}
