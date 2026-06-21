// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Data/Move_LocomotionProfile.h"

float UMove_LocomotionProfile::ResolveMaxSpeed(const FMove_DomainTuning& Domain, float SettingsFallback) const
{
	// A profile row of <= 0 is the documented "use project fallback" sentinel.
	return Domain.MaxSpeed > 0.f ? Domain.MaxSpeed : SettingsFallback;
}
