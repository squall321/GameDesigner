// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

/**
 * Native (C++-defined) anchor tags for the DesignPatternsWorld module.
 *
 * These anchor the world-hub channels and service keys UNDER the core's roots so that
 * tag-hierarchy matching always works at startup:
 *  - DP.Service.WorldHub.*  service-locator keys for the hub subsystems/providers.
 *  - DP.Bus.WorldHub.*      message-bus channels the hub broadcasts on when state changes.
 *
 * Concrete flag keys themselves (DP.WorldHub.Flag.* and friends) are authored by the game
 * project / its flag-set data assets, NOT here — this header only anchors the service and
 * bus roots the hub plumbing relies on. Full tag strings live in WorldHub_NativeTags.cpp.
 */
namespace WorldHubNativeTags
{
	// --- Service-locator keys (children of the core DP.Service root) ---

	/** Service key under which the world state-hub subsystem registers itself. */
	DESIGNPATTERNSWORLD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_WorldHub);

	/** Service key for the read-only query seam (IWorldHub_Queryable) exposed by the hub. */
	DESIGNPATTERNSWORLD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_WorldHub_Query);

	/** Service key under which dynamic state providers (IWorldHub_StateProvider) aggregate. */
	DESIGNPATTERNSWORLD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_WorldHub_Provider);

	// --- Message-bus channels (children of the core DP.Bus root) ---

	/** Bus channel: broadcast when any hub flag value changes (payload carries key + scope). */
	DESIGNPATTERNSWORLD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_WorldHub_FlagChanged);

	/** Bus channel: broadcast when a hub counter changes (payload carries key + scope + delta). */
	DESIGNPATTERNSWORLD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_WorldHub_CounterChanged);

	/** Bus channel: broadcast when a scoped blackboard value changes (key + scope + source). */
	DESIGNPATTERNSWORLD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_WorldHub_BlackboardChanged);
}
