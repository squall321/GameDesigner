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

	/** Service key for the hub history / rewind seam (ISeam_HubHistory) exposed by the history subsystem. */
	DESIGNPATTERNSWORLD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_WorldHub_History);

	/** Service key for the append-only mutation event log (also ISeam_HubHistory). */
	DESIGNPATTERNSWORLD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_WorldHub_EventLog);

	/** Service key for the faction-vs-faction standing matrix (ISeam_FactionStanding). */
	DESIGNPATTERNSWORLD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_WorldHub_FactionMatrix);

	// --- Message-bus channels (children of the core DP.Bus root) ---

	/** Bus channel: broadcast when any hub flag value changes (payload carries key + scope). */
	DESIGNPATTERNSWORLD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_WorldHub_FlagChanged);

	/** Bus channel: broadcast when a hub counter changes (payload carries key + scope + delta). */
	DESIGNPATTERNSWORLD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_WorldHub_CounterChanged);

	/** Bus channel: broadcast when a scoped blackboard value changes (key + scope + source). */
	DESIGNPATTERNSWORLD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_WorldHub_BlackboardChanged);

	/** Bus channel: broadcast when the history subsystem captures a frame (payload carries frame index + label). */
	DESIGNPATTERNSWORLD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_WorldHub_FrameCaptured);

	/** Bus channel: broadcast when world state is rewound to a frame/checkpoint (payload carries label). */
	DESIGNPATTERNSWORLD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_WorldHub_Rewound);

	/** Bus channel: broadcast when a faction standing changes (payload carries the composed key + value). */
	DESIGNPATTERNSWORLD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_WorldHub_StandingChanged);

	// --- Composed-key roots for projected sub-systems (children of the project flag root) ---

	/**
	 * Root under which the faction-matrix component composes its per-(A,B) hub keys
	 * (DP.WorldHub.Faction.Standing.<A>.<B>). Anchored in C++ so MatchesTag works at startup and so
	 * subscription filters can target the whole faction sub-tree.
	 */
	DESIGNPATTERNSWORLD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Key_WorldHub_FactionStanding);
}
