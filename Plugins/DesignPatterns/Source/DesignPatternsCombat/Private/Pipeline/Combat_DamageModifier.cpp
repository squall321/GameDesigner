// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Pipeline/Combat_DamageModifier.h"

void UCombat_DamageModifier::Modify_Implementation(FCombat_DamageContext& /*Context*/) const
{
	// Base modifier is a no-op; concrete leaves do the work. Kept non-pure-virtual so a designer can
	// drop a bare base instance as a documented pass-through entry in a list without crashing.
}

bool UCombat_DamageModifier::MatchesChannel(const FCombat_DamageContext& Context) const
{
	if (AppliesToDamageTypes.IsEmpty())
	{
		return true;
	}

	FGameplayTagContainer Channels;
	if (Context.DamageChannel.IsValid())
	{
		Channels.AddTag(Context.DamageChannel);
	}
	return AppliesToDamageTypes.Matches(Channels);
}
