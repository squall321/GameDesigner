// Copyright DesignPatterns plugin. All Rights Reserved.

#include "HUD_DeepNativeTags.h"

namespace HUDTags
{
	// --- Service-locator keys under the core DP.Service root ---
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Service_StatusProvider, "DP.Service.HUD.StatusProvider",
		"Key under which the game publishes its ISeam_StatusProvider for the HUD status bar.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Service_ObjectiveSource, "DP.Service.HUD.ObjectiveSource",
		"Key under which the game publishes its ISeam_ObjectiveSource for the HUD objective tracker.");

	// --- LOCAL/COSMETIC bus channels under the core DP.Bus root ---
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_HUD_ReticleSpread, "DP.Bus.HUD.ReticleSpread",
		"A producer publishes the local viewer's reticle spread (single float field, name tunable on config).");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_HUD_ObjectiveChanged, "DP.Bus.HUD.ObjectiveChanged",
		"A producer signals that objectives changed so the tracker can refresh.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_HUD_HealthFraction, "DP.Bus.HUD.HealthFraction",
		"A producer publishes the local viewer's health fraction (single float field, name tunable on config).");

	// --- Marker-kind tags under this module's DP.HUD.Marker hierarchy ---
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Marker_Threat, "DP.HUD.Marker.Threat",
		"Marker kind: a threat / danger world indicator (off-screen arrow + on-screen marker).");

	// --- Reticle target-type tags under this module's DP.HUD.Reticle.Target hierarchy ---
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Reticle_Target_Friendly, "DP.HUD.Reticle.Target.Friendly",
		"Reticle target type: the centre target is friendly to the local pawn.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Reticle_Target_Hostile, "DP.HUD.Reticle.Target.Hostile",
		"Reticle target type: the centre target is hostile to the local pawn.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Reticle_Target_Neutral, "DP.HUD.Reticle.Target.Neutral",
		"Reticle target type: the centre target is neutral / no team relation resolvable.");
}
