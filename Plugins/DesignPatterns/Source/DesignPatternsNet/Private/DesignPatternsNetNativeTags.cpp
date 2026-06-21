// Copyright DesignPatterns plugin. All Rights Reserved.

#include "DesignPatternsNetNativeTags.h"

namespace NetNativeTags
{
	// ---- Service-locator keys ----
	UE_DEFINE_GAMEPLAY_TAG(Service_Net_LagComp,        "DP.Service.Net.LagComp");
	UE_DEFINE_GAMEPLAY_TAG(Service_Net_Lobby,          "DP.Service.Net.Lobby");
	UE_DEFINE_GAMEPLAY_TAG(Service_Net_Scoreboard,     "DP.Service.Net.Scoreboard");

	// ---- Message-bus channels (under the core DP.Bus root) ----
	UE_DEFINE_GAMEPLAY_TAG(Bus_Net,                    "DP.Bus.Net");
	UE_DEFINE_GAMEPLAY_TAG(Bus_Net_Lobby_Changed,      "DP.Bus.Net.Lobby.Changed");
	UE_DEFINE_GAMEPLAY_TAG(Bus_Net_Lobby_AllReady,     "DP.Bus.Net.Lobby.AllReady");
	UE_DEFINE_GAMEPLAY_TAG(Bus_Net_HostMigration,      "DP.Bus.Net.HostMigration");
	UE_DEFINE_GAMEPLAY_TAG(Bus_Net_HitConfirmed,       "DP.Bus.Net.HitConfirmed");
	UE_DEFINE_GAMEPLAY_TAG(Bus_Net_AntiCheat_Flagged,  "DP.Bus.Net.AntiCheat.Flagged");

	// ---- Lobby phase anchors ----
	UE_DEFINE_GAMEPLAY_TAG(Net_Lobby_Phase,            "DP.Net.Lobby.Phase");
	UE_DEFINE_GAMEPLAY_TAG(Net_Lobby_Phase_Filling,    "DP.Net.Lobby.Phase.Filling");
	UE_DEFINE_GAMEPLAY_TAG(Net_Lobby_Phase_Countdown,  "DP.Net.Lobby.Phase.Countdown");
	UE_DEFINE_GAMEPLAY_TAG(Net_Lobby_Phase_Starting,   "DP.Net.Lobby.Phase.Starting");
	UE_DEFINE_GAMEPLAY_TAG(Net_Lobby_Party,            "DP.Net.Lobby.Party");
}
