// Copyright DesignPatterns plugin. All Rights Reserved.

#include "HUD_NativeTags.h"

namespace HUDTags
{
	// Service-locator keys live under the core DP.Service root so the locator can list them
	// alongside every other published provider.
	UE_DEFINE_GAMEPLAY_TAG(Service_MarkerRegistry, "DP.Service.HUD.MarkerRegistry");

	// Shared cross-module contract key: the Platform module publishes its ISeam_InputModeArbiter here.
	UE_DEFINE_GAMEPLAY_TAG(Service_InputModeArbiter, "DP.Service.Input.ModeArbiter");

	// Input-mode identities under the SHARED DP.Input.Mode root (the arbiter, owned by Platform,
	// keys its priority stack by these opaque mode tags).
	UE_DEFINE_GAMEPLAY_TAG(InputMode_Menu,     "DP.Input.Mode.Menu");
	UE_DEFINE_GAMEPLAY_TAG(InputMode_Dialogue, "DP.Input.Mode.Dialogue");

	// Enhanced-Input context layer tags under this module's DP.HUD.InputLayer root.
	UE_DEFINE_GAMEPLAY_TAG(InputLayer_Gameplay, "DP.HUD.InputLayer.Gameplay");
	UE_DEFINE_GAMEPLAY_TAG(InputLayer_Menu,     "DP.HUD.InputLayer.Menu");
	UE_DEFINE_GAMEPLAY_TAG(InputLayer_Dialogue, "DP.HUD.InputLayer.Dialogue");

	// Message-bus channels under the core DP.Bus root.
	UE_DEFINE_GAMEPLAY_TAG(Bus_InputIntent, "DP.Bus.HUD.Input");
	UE_DEFINE_GAMEPLAY_TAG(Bus_MenuBack,    "DP.Bus.HUD.Menu.Back");
	UE_DEFINE_GAMEPLAY_TAG(Bus_MenuPush,    "DP.Bus.HUD.Menu.Push");

	// Canonical marker-kind tags under this module's DP.HUD.Marker root.
	UE_DEFINE_GAMEPLAY_TAG(Marker_Player,         "DP.HUD.Marker.Player");
	UE_DEFINE_GAMEPLAY_TAG(Marker_Objective,      "DP.HUD.Marker.Objective");
	UE_DEFINE_GAMEPLAY_TAG(Marker_Enemy,          "DP.HUD.Marker.Enemy");
	UE_DEFINE_GAMEPLAY_TAG(Marker_PointOfInterest, "DP.HUD.Marker.PointOfInterest");
}
