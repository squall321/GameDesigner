// Copyright DesignPatterns plugin. All Rights Reserved.

#include "DesignPatternsLevelDirectorNativeTags.h"

namespace LvlNativeTags
{
	// Message-bus channels: anchored under the core DP.Bus root so the core message bus' hierarchy
	// matching delivers them to broad listeners (e.g. one subscribed at DP.Bus or DP.Bus.Lvl).
	UE_DEFINE_GAMEPLAY_TAG(Bus_Lvl_Placement_Generated,  "DP.Bus.Lvl.Placement.Generated");
	UE_DEFINE_GAMEPLAY_TAG(Bus_Lvl_Placement_Cleared,    "DP.Bus.Lvl.Placement.Cleared");
	UE_DEFINE_GAMEPLAY_TAG(Bus_Lvl_Encounter_Activated,  "DP.Bus.Lvl.Encounter.Activated");
	UE_DEFINE_GAMEPLAY_TAG(Bus_Lvl_Encounter_Deactivated,"DP.Bus.Lvl.Encounter.Deactivated");

	// Service-locator keys, anchored under the core DP.Service root.
	UE_DEFINE_GAMEPLAY_TAG(Service_Lvl_ActivationGate,   "DP.Service.Lvl.ActivationGate");
	UE_DEFINE_GAMEPLAY_TAG(Service_Lvl_TileProvider,     "DP.Service.Lvl.TileProvider");

	// Persistence record kinds, anchored under the core DP.Persist root.
	UE_DEFINE_GAMEPLAY_TAG(Persist_Lvl_Placement,        "DP.Persist.Lvl.Placement");
}
