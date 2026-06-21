// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Net/Seam_HitRewindTarget.h"

// Native defaults for the BlueprintNativeEvent seam. Implementers (Combat's
// UCombat_HitRewindTargetComponent) override these; the fail-closed defaults below ensure a
// half-implemented target never silently confirms damage.

FSeam_EntityId ISeam_HitRewindTarget::GetRewindEntityId_Implementation() const
{
	return FSeam_EntityId::Invalid();
}

bool ISeam_HitRewindTarget::GetRewindBounds_Implementation(FBoxSphereBounds& OutBounds) const
{
	OutBounds = FBoxSphereBounds(ForceInit);
	return false;
}

void ISeam_HitRewindTarget::ApplyConfirmedHit_Implementation(AActor* /*Instigator*/, FGameplayTag /*DamageChannel*/, FSeam_NetValue /*Magnitude*/, FName /*HitBoneName*/)
{
	// Default: no-op. A real damageable overrides this to route into its health component.
}
