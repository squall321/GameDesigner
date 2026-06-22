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

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Service_WorldHub_History, "DP.Service.WorldHub.History",
		"Service-locator key for the world-hub history/rewind seam (ISeam_HubHistory).");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Service_WorldHub_EventLog, "DP.Service.WorldHub.EventLog",
		"Service-locator key for the append-only world-hub mutation event log (ISeam_HubHistory).");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Service_WorldHub_FactionMatrix, "DP.Service.WorldHub.FactionMatrix",
		"Service-locator key for the faction-vs-faction standing matrix (ISeam_FactionStanding).");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_WorldHub_FlagChanged, "DP.Bus.WorldHub.FlagChanged",
		"Broadcast when a world-hub flag value changes; payload carries the key and scope.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_WorldHub_CounterChanged, "DP.Bus.WorldHub.CounterChanged",
		"Broadcast when a world-hub counter changes; payload carries key, scope and delta.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_WorldHub_BlackboardChanged, "DP.Bus.WorldHub.BlackboardChanged",
		"Broadcast when a scoped-blackboard value changes; payload carries key, scope and source.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_WorldHub_FrameCaptured, "DP.Bus.WorldHub.FrameCaptured",
		"Broadcast when the history subsystem captures a snapshot frame; payload carries frame index and label.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_WorldHub_Rewound, "DP.Bus.WorldHub.Rewound",
		"Broadcast when world-hub state is rewound to a frame/checkpoint; payload carries the checkpoint label.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_WorldHub_StandingChanged, "DP.Bus.WorldHub.StandingChanged",
		"Broadcast when a faction-vs-faction standing changes; payload carries the composed key and new value.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Key_WorldHub_FactionStanding, "DP.WorldHub.Faction.Standing",
		"Composed-key root for per-(A,B) faction standings projected into the hub registry.");
}
