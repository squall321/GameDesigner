// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Hit/Combat_DamageExecution.h"

bool UCombat_DamageExecution::RollCritical() const
{
	return CritChance > 0.f && FMath::FRand() < CritChance;
}

float UCombat_DamageExecution::CalculateDamage_Implementation(const FCombat_HitResult& Hit) const
{
	float Damage = FMath::Max(0.f, Hit.BaseDamage);

	// True damage bypasses any further modification.
	if (Hit.DamageType == ECombat_DamageType::True)
	{
		return Damage;
	}

	if (RollCritical())
	{
		Damage *= FMath::Max(1.f, CritMultiplier);
	}

	return Damage;
}
