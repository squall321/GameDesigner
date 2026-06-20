// Copyright DesignPatterns plugin. All Rights Reserved.

#include "WorldHub_NativeTags.h"

namespace WorldHubNativeTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Service_WorldHub, "DP.Service.WorldHub",
		"Service-locator key for the world state-hub subsystem (UWorldHub_StateHubSubsystem).");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Service_WorldHub_Query, "DP.Service.WorldHub.Query",
		"Service-locator key for the world hub read seam (IWorldHub_Queryable).");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Service_WorldHub_Provider, "DP.Service.WorldHub.Provider",
		"Service-locator key for aggregated dynamic state providers (IWorldHub_StateProvider).");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_WorldHub_FlagChanged, "DP.Bus.WorldHub.FlagChanged",
		"Broadcast when a world-hub flag value changes; payload carries the key and scope.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_WorldHub_CounterChanged, "DP.Bus.WorldHub.CounterChanged",
		"Broadcast when a world-hub counter changes; payload carries key, scope and delta.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_WorldHub_BlackboardChanged, "DP.Bus.WorldHub.BlackboardChanged",
		"Broadcast when a scoped-blackboard value changes; payload carries key, scope and source.");
}
