// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Pipeline/Combat_ResistanceProfile.h"
#include "Combat_DeepNativeTags.h"

float UCombat_ResistanceProfile::GetResistFraction(FGameplayTag Channel) const
{
	float Resist = 0.f;

	// Per-channel authored entries.
	for (const FCombat_ResistanceEntry& Entry : Resistances)
	{
		if (Entry.DamageChannel == Channel)
		{
			Resist += Entry.ResistFraction;
		}
	}

	// Armor adds to physical resistance only.
	if (Channel == CombatDeepNativeTags::Damage_Physical)
	{
		Resist += Armor * ArmorEffectiveness;
	}

	// Defensive clamp: never exceed the authored cap, never below zero from the additive entries.
	return FMath::Clamp(Resist, 0.f, FMath::Clamp(MaxResistFraction, 0.f, 1.f));
}
