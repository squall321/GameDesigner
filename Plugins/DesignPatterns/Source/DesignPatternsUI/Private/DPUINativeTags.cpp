// Copyright DesignPatterns plugin. All Rights Reserved.

#include "DPUINativeTags.h"

namespace DPUITags
{
	// Shared platform seam keys live under the core DP.Service root so the locator can list
	// them alongside every other published provider. These exact strings are the verified
	// keys the Platform module registers under.
	UE_DEFINE_GAMEPLAY_TAG(Service_SafeZone,              "DP.Service.Platform.SafeZone");
	UE_DEFINE_GAMEPLAY_TAG(Service_Glyphs,                "DP.Service.Platform.Glyphs");
	UE_DEFINE_GAMEPLAY_TAG(Service_AccessibilityProvider, "DP.Service.Loc.AccessibilityProvider");
	UE_DEFINE_GAMEPLAY_TAG(Service_UIHighlight,           "DP.Service.UI.Highlight");

	// UI-owned presenter key (defined here, in the UI module — never in Seams).
	UE_DEFINE_GAMEPLAY_TAG(Service_Notice,                "DP.Service.UI.Notice");

	// Message-bus channels under the core DP.Bus root.
	UE_DEFINE_GAMEPLAY_TAG(Bus_DragDropCompleted,  "DP.Bus.UI.DragDrop.Completed");
	UE_DEFINE_GAMEPLAY_TAG(Bus_InputDeviceChanged, "DP.Bus.UI.InputDeviceChanged");
	UE_DEFINE_GAMEPLAY_TAG(Bus_BreakpointChanged,  "DP.Bus.UI.BreakpointChanged");
}
