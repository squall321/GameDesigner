// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Effect/Combat_StatusFamilyEffect.h"

float UCombat_StatusFamilyEffect::GetDurationMultiplierForStack(int32 ExistingCount) const
{
	if (!DiminishingReturnsCurve)
	{
		return 1.f; // defensive fallback: no DR authored => full duration
	}
	const float Mult = DiminishingReturnsCurve->GetFloatValue(static_cast<float>(FMath::Max(0, ExistingCount)));
	return FMath::Clamp(Mult, 0.f, 1.f);
}
