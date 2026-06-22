// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Shake/Cam_ShakeProfile.h"

#include "Curves/CurveFloat.h"

UCam_ShakeProfile::UCam_ShakeProfile()
{
}

float UCam_ShakeProfile::ComputeScaleAtDistance(float Distance) const
{
	const float Inner = FMath::Max(InnerRadius, 0.f);
	const float Outer = FMath::Max(OuterRadius, Inner); // defensive: Outer never below Inner.

	if (Distance <= Inner)
	{
		return 1.f;
	}
	if (Distance >= Outer)
	{
		return 0.f;
	}

	// Normalised position in the falloff band [0,1]: 0 at Inner, 1 at Outer.
	const float Range = FMath::Max(Outer - Inner, KINDA_SMALL_NUMBER);
	const float NormDistance = FMath::Clamp((Distance - Inner) / Range, 0.f, 1.f);

	if (DistanceFalloff)
	{
		// Curve authored as distance(0..1) -> scale(0..1).
		return FMath::Clamp(DistanceFalloff->GetFloatValue(NormDistance), 0.f, 1.f);
	}

	// Default: linear falloff (1 at Inner, 0 at Outer).
	return 1.f - NormDistance;
}

FName UCam_ShakeProfile::GetDataAssetType_Implementation() const
{
	// Own asset-manager bucket so projects can preload all shake profiles together.
	return FName("Cam_ShakeProfile");
}
