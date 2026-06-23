// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

/**
 * Native (C++-defined) anchor tags for the DesignPatternsUI deepening layer.
 *
 * These are the service-locator keys and message-bus channels this module's own
 * code references literally at runtime. They are anchored here so:
 *  - the shared platform seams (safe-zone, glyphs) resolve under STABLE, verified
 *    keys (DP.Service.Platform.SafeZone / DP.Service.Platform.Glyphs) rather than
 *    raw FName literals scattered through .cpp, AND
 *  - the optional UI presenter services (toast/notice) publish under a key that
 *    is defined IN THE UI MODULE — never in the leaf Seams module — keeping Seams
 *    free of any UI service keys.
 *
 * Everything designer-facing (per-game breakpoints, easing curves, drop-zone
 * payload types, focus group identities) flows through data assets / EditAnywhere
 * UPROPERTY tunables as opaque FGameplayTag values and is NEVER hard-coded here.
 */
namespace DPUITags
{
	// --- Shared platform seam service keys (providers registered by the Platform module) ---

	/**
	 * Service-locator key under which the shared ISeam_SafeZoneProvider is published.
	 * The responsive-layout subsystem resolves a TScriptInterface<ISeam_SafeZoneProvider>
	 * from this key (held weakly, no-op when unset). Cross-module contract key: this
	 * module only REFERENCES it; the provider is registered elsewhere (Platform).
	 */
	DESIGNPATTERNSUI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_SafeZone);

	/**
	 * Service-locator key under which the shared ISeam_InputGlyphProvider is published.
	 * Input-prompt widgets resolve a TScriptInterface<ISeam_InputGlyphProvider> from
	 * this key to draw the correct device glyph + label.
	 */
	DESIGNPATTERNSUI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_Glyphs);

	/**
	 * Service-locator key under which the Localization accessibility provider is
	 * published. The responsive-layout subsystem resolves the provider here once to
	 * register ITSELF as an ISeam_AccessibilityConsumer (push handshake), since the
	 * accessibility seam is push-only and requires registration to receive updates.
	 */
	DESIGNPATTERNSUI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_AccessibilityProvider);

	/**
	 * Service-locator key under which the shared ISeam_UIHighlight provider is published.
	 * Reserved for UI presenters that surface tutorial/onboarding highlights.
	 */
	DESIGNPATTERNSUI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_UIHighlight);

	// --- UI-owned presenter service keys (DEFINED in this module, published by UI presenters) ---

	/**
	 * Service-locator key under which a UI toast/notice presenter publishes itself.
	 * Defined IN the UI module (not in Seams) so the leaf Seams module never carries a
	 * UI service key. Optional: only meaningful when a presenter is registered.
	 */
	DESIGNPATTERNSUI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_Notice);

	// --- Message-bus channels (drag/drop results + device-change refresh) ---
	// All UI bus traffic is local/cosmetic-or-intent and anchored under the core DP.Bus root.

	/** Channel a completed drag-drop result is published on (payload = FDP_DragDropResultPayload). */
	DESIGNPATTERNSUI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_DragDropCompleted);

	/**
	 * Channel an input-device change is broadcast on (no payload required). Input-prompt
	 * widgets listen here to re-resolve their glyph for the new device family.
	 */
	DESIGNPATTERNSUI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_InputDeviceChanged);

	/** Channel the responsive-layout subsystem publishes a breakpoint change on (cosmetic). */
	DESIGNPATTERNSUI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_BreakpointChanged);
}
