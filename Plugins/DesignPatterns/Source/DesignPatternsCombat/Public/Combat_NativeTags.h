// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

/**
 * Native (C++-defined) tags for the DesignPatternsCombat module.
 *
 * These anchor concrete combat channels UNDER the core's DP.Bus root so tag-hierarchy
 * matching works (a listener on DP.Bus or DP.Bus.Combat receives DP.Bus.Combat.Death).
 * The full tag strings are defined in Combat_NativeTags.cpp.
 */
namespace CombatNativeTags
{
	// Bus channel: broadcast when a UCombat_HealthComponent's owner dies.
	DESIGNPATTERNSCOMBAT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Combat_Death);

	// Bus channel: broadcast when a UCombat_HealthComponent takes damage.
	DESIGNPATTERNSCOMBAT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Combat_Damaged);

	// Bus channel: broadcast when a status effect is applied to an actor.
	DESIGNPATTERNSCOMBAT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Combat_StatusApplied);

	// Bus channel: broadcast when a status effect is removed from an actor.
	DESIGNPATTERNSCOMBAT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Combat_StatusRemoved);
}
