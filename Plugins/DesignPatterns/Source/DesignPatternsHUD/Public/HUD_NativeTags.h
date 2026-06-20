// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

/**
 * Native anchor tags for the DesignPatternsHUD module (minimap/markers + menu navigation + input context).
 *
 * These are the C++-defined roots/leaves the HUD framework references literally at runtime:
 *  - the service-locator key under which the live world marker registry publishes itself, so the
 *    minimap ViewModel can resolve trackables without depending on any actor type;
 *  - the input-mode tags this module PUSHES through the shared ISeam_InputModeArbiter while a menu
 *    or dialogue layer owns input;
 *  - the canonical Enhanced-Input *context layer* tags (gameplay / menu / dialogue) that the input
 *    context subsystem layers by priority;
 *  - the message-bus channels the input context subsystem publishes mapped input *intents* on, plus
 *    the menu navigation intents the menu stack reacts to;
 *  - a small set of canonical marker-kind tags so the projection/ViewModel layer has stable, shipped
 *    icon keys to project against.
 *
 * Everything designer-facing (per-game marker kinds, per-game action->intent rows, per-game mapping
 * contexts) flows through data assets / settings as opaque FGameplayTag values and soft refs and is
 * NEVER hard-coded here. This header only anchors the tags this module's own code mentions by name.
 */
namespace HUDTags
{
	// --- Service-locator keys (published by this module's world subsystems) ---

	/** Service-locator key for the live UHUD_MarkerRegistrySubsystem (world-scoped, weak-observed). */
	DESIGNPATTERNSHUD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_MarkerRegistry);

	/**
	 * Service-locator key under which the shared ISeam_InputModeArbiter is published (by the Platform
	 * module). The menu stack resolves a TScriptInterface<ISeam_InputModeArbiter> from this key to push/pop
	 * the menu input mode without depending on the Platform concrete type. This is a shared, cross-module
	 * contract tag; the HUD module only references it (the provider is registered elsewhere).
	 */
	DESIGNPATTERNSHUD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_InputModeArbiter);

	// --- Input-mode tags pushed through the shared ISeam_InputModeArbiter ---
	// The arbiter is owned by the Platform module; these are the mode identities the HUD requests
	// while a menu / dialogue layer holds input. Anchored under the shared DP.Input.Mode root.

	/** Input mode requested while a menu screen owns the input stack (UI-only navigation). */
	DESIGNPATTERNSHUD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputMode_Menu);

	/** Input mode requested while a dialogue presentation layer owns input (game-and-UI). */
	DESIGNPATTERNSHUD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputMode_Dialogue);

	// --- Enhanced-Input context *layer* tags (priority-ordered by the context subsystem) ---
	// A game registers a mapping context against one of these layer tags (or its own child) in the
	// action-map data asset; the subsystem adds/removes them on the player's EnhancedInput subsystem
	// at the layer's priority. Anchored under DP.HUD.InputLayer.

	/** Base gameplay input layer (lowest priority — always-on locomotion/interaction). */
	DESIGNPATTERNSHUD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputLayer_Gameplay);

	/** Menu/navigation input layer (added on top of gameplay while a menu is open). */
	DESIGNPATTERNSHUD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputLayer_Menu);

	/** Dialogue input layer (added while a conversation is being presented). */
	DESIGNPATTERNSHUD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputLayer_Dialogue);

	// --- Message-bus channels (mapped input intents + menu navigation) ---
	// All HUD bus traffic is local/cosmetic-or-intent and anchored under the core DP.Bus root.

	/** Root channel the input context subsystem publishes mapped action intents on (DP.Bus.HUD.Input). */
	DESIGNPATTERNSHUD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_InputIntent);

	/** Menu navigation: a "back / cancel" intent the menu stack consumes to pop the top screen. */
	DESIGNPATTERNSHUD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_MenuBack);

	/** Menu navigation: a request to push a named screen (payload carries the screen tag). */
	DESIGNPATTERNSHUD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_MenuPush);

	// --- Canonical marker-kind tags (stable shipped icon keys for the minimap projection) ---
	// Games extend the DP.HUD.Marker hierarchy in their own tag config; these are only the roots
	// this module's debug/default projection references by name.

	/** Marker kind: the local player / self blip. */
	DESIGNPATTERNSHUD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Marker_Player);

	/** Marker kind: an objective / quest waypoint. */
	DESIGNPATTERNSHUD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Marker_Objective);

	/** Marker kind: a hostile contact. */
	DESIGNPATTERNSHUD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Marker_Enemy);

	/** Marker kind: a point of interest / generic location. */
	DESIGNPATTERNSHUD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Marker_PointOfInterest);
}
