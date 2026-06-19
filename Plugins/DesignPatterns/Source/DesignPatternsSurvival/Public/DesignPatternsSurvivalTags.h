// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

/**
 * Native (C++-defined) anchor tags for the DesignPatternsSurvival module.
 *
 * These are ROOT/anchor tags only — concrete resources, needs and stations are authored by the
 * game project as child tags. Anchoring the roots here guarantees the hierarchy exists at
 * startup so tag-hierarchy matching always works (e.g. a station tag "Surv.Station.Forge"
 * matches a recipe requiring "Surv.Station").
 */
namespace SurvNativeTags
{
	/** Root for harvestable resource identities (e.g. Surv.Resource.Wood). */
	DESIGNPATTERNSSURVIVAL_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Resource);

	/** Root for survival needs (Surv.Need.Hunger / Thirst / Stamina / Temperature). */
	DESIGNPATTERNSSURVIVAL_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Need);

	/** Root for crafting-station kinds (Surv.Station.Workbench, Surv.Station.Forge...). */
	DESIGNPATTERNSSURVIVAL_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Station);

	/** Root for message-bus channels broadcast by this module (children of DP.Bus by convention). */
	DESIGNPATTERNSSURVIVAL_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus);
}
