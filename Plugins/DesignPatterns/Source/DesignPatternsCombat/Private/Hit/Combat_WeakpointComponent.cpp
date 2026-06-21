// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Hit/Combat_WeakpointComponent.h"

UCombat_WeakpointComponent::UCombat_WeakpointComponent()
{
	// Pure query surface — no tick, no replication.
	PrimaryComponentTick.bCanEverTick = false;
}

bool UCombat_WeakpointComponent::QueryZone(FName BoneName, float& OutMultiplier, FGameplayTag& OutZoneTag, bool& OutIsWeakpoint) const
{
	OutMultiplier = 1.f;
	OutZoneTag = FGameplayTag();
	OutIsWeakpoint = false;

	if (BoneName.IsNone())
	{
		return false;
	}

	for (const FCombat_HitZone& Zone : Zones)
	{
		if (Zone.BoneName == BoneName)
		{
			OutMultiplier = Zone.DamageMultiplier;
			OutZoneTag = Zone.ZoneTag;
			OutIsWeakpoint = Zone.bIsWeakpoint;
			return true;
		}
	}

	return false;
}
