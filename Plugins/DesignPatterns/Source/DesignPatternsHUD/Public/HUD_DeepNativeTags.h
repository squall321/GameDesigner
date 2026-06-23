// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

/**
 * Native anchor tags for the DesignPatternsHUD module's DEEP combat/feedback area (floating damage numbers,
 * off/on-screen world indicators, status bar, reticle, objective tracker, full-screen state effects).
 *
 * These EXTEND the module's single, open HUDTags namespace (precedent: HUD_HudNotifyTags.h) with the tags
 * this deepening layer references literally at runtime:
 *  - service-locator keys the new subsystems resolve their seam providers under (status provider, objective
 *    source, optional health-fraction source) — exactly as the minimap resolves Service_MarkerRegistry;
 *  - the LOCAL/COSMETIC message-bus channels the deep HUD subsystems publish on (reticle/objective/health),
 *    all anchored under the core DP.Bus root so producers stay decoupled;
 *  - canonical marker-kind tags for threat/objective world indicators (icon keys for the indicator layer);
 *  - the reticle target-type tags the reticle maps team affinity to (friendly / hostile / neutral).
 *
 * Everything designer-facing (per-game styles, per-game thresholds, per-game channels to observe) flows
 * through data assets / settings as opaque FGameplayTag values and is NEVER hard-coded here. The full tag
 * strings are defined in HUD_DeepNativeTags.cpp (declare + define pair — no declare-only).
 */
namespace HUDTags
{
	// --- Service-locator keys (resolved by the deep HUD subsystems) ---

	/**
	 * Service-locator key under which the game's ISeam_StatusProvider is published. The status-bar ViewModel
	 * resolves a TScriptInterface<ISeam_StatusProvider> from this key (or reads one off the local pawn) to
	 * project active buffs/debuffs without depending on the concrete stats module.
	 */
	DESIGNPATTERNSHUD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_StatusProvider);

	/**
	 * Service-locator key under which the game's ISeam_ObjectiveSource is published. The objective-tracker
	 * subsystem resolves a TScriptInterface<ISeam_ObjectiveSource> from this key to project tracked
	 * objectives without depending on the concrete quest module.
	 */
	DESIGNPATTERNSHUD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_ObjectiveSource);

	// --- Deep HUD bus channels (local/cosmetic, anchored under the core DP.Bus root) ---

	/**
	 * Bus channel a producer publishes the local viewer's reticle spread on (payload carries a single float
	 * field whose name is tunable on the reticle config asset). The reticle subsystem listens here so weapon
	 * systems can drive spread without the HUD depending on them.
	 */
	DESIGNPATTERNSHUD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_HUD_ReticleSpread);

	/** Bus channel a producer publishes objective changes on (so the tracker can refresh eagerly). */
	DESIGNPATTERNSHUD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_HUD_ObjectiveChanged);

	/**
	 * Bus channel a producer publishes the local viewer's health fraction on (payload carries a single float
	 * field whose name is tunable on the screen-state config asset). The screen-state subsystem listens here
	 * so a health system can drive the low-health vignette without the HUD depending on it.
	 */
	DESIGNPATTERNSHUD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_HUD_HealthFraction);

	// --- Canonical marker-kind tags for world indicators (icon keys for the indicator layer) ---

	/** Marker kind: a threat / danger world indicator (off-screen arrow + on-screen marker). */
	DESIGNPATTERNSHUD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Marker_Threat);

	// --- Reticle target-type tags (team affinity -> reticle coloring) ---

	/** Reticle target type: the centre target is friendly to the local pawn. */
	DESIGNPATTERNSHUD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Reticle_Target_Friendly);

	/** Reticle target type: the centre target is hostile to the local pawn. */
	DESIGNPATTERNSHUD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Reticle_Target_Hostile);

	/** Reticle target type: the centre target is neutral / no team relation resolvable. */
	DESIGNPATTERNSHUD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Reticle_Target_Neutral);
}
