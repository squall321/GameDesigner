// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Combat_NativeTags.h"

namespace CombatNativeTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_Combat_Death, "DP.Bus.Combat.Death",
		"Broadcast when a UCombat_HealthComponent's owner dies.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_Combat_Damaged, "DP.Bus.Combat.Damaged",
		"Broadcast when a UCombat_HealthComponent takes damage.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_Combat_StatusApplied, "DP.Bus.Combat.StatusApplied",
		"Broadcast when a status effect is applied to an actor.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_Combat_StatusRemoved, "DP.Bus.Combat.StatusRemoved",
		"Broadcast when a status effect is removed from an actor.");
}
