// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

/**
 * Native anchor tags owned by the market / economy-driver / save area of DesignPatternsSimEconomy.
 *
 * Kept in a SEPARATE namespace from SimEcoNativeTags (which the commodity/settings area owns) so the
 * two areas never collide on UE_DEFINE_GAMEPLAY_TAG. These cover the persistence-kind routing tags
 * and the service-locator key under which the shared sim-clock seam is published.
 */
namespace SimEcoEconomyTags
{
	/** Persistence-kind root for this area's ISeam_Persistable participants (SimEco.Persist.*). */
	DESIGNPATTERNSSIMECONOMY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Persist);

	/** Persistence kind for the market subsystem's price book. */
	DESIGNPATTERNSSIMECONOMY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Persist_Market);

	/**
	 * Service-locator key under which whoever owns the authoritative clock publishes its
	 * ISeam_SimClock (typically the Survival day-night clock). The economy driver resolves it here.
	 */
	DESIGNPATTERNSSIMECONOMY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_SimClock);
}
