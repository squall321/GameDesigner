// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Modules/ModuleManager.h"
#include "DesignPatternsGameModeModule.h"
#include "Core/DPLog.h"

//~ Native gameplay tag definitions ----------------------------------------------------------------

namespace GameModeNativeTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GM, "DP.GM",
		"Root anchor for the DesignPatternsGameMode module's tags.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Match, "DP.GM.Match",
		"Parent of the match-state FSM tags; never set as an active state itself.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Match_WaitingToStart, "DP.GM.Match.WaitingToStart",
		"Pre-match warmup: waiting for the start condition to be met.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Match_InProgress, "DP.GM.Match.InProgress",
		"Active play: ruleset win/lose conditions evaluate each authority tick.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Match_RoundOver, "DP.GM.Match.RoundOver",
		"A round ended; intermission before the next round (match continues).");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Match_MatchOver, "DP.GM.Match.MatchOver",
		"The match is fully decided; scores are final and a results screen is safe.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Score, "DP.Score",
		"Root anchor for tag-keyed scoreboard buckets.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Score_Default, "DP.Score.Default",
		"Default neutral score bucket used when no team/category key is supplied.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Service_GM_Score, "DP.Service.GM.Score",
		"Service-locator key under which the score carrier registers ISeam_ScoreSource.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Service_GM_TeamAffinity, "DP.Service.GM.TeamAffinity",
		"Service-locator key under which UGM_TeamSubsystem publishes its ISeam_TeamAffinity provider.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_GM_MatchStateChanged, "DP.Bus.GM.MatchStateChanged",
		"Broadcast when the match state changes; payload carries old and new state tags.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_GM_ScoreChanged, "DP.Bus.GM.ScoreChanged",
		"Broadcast when a score bucket changes; payload carries key, value and delta.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_GM_MatchDecided, "DP.Bus.GM.MatchDecided",
		"Broadcast when the match is decided; payload carries the winning key (empty = draw).");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_GM_TeamChanged, "DP.Bus.GM.TeamChanged",
		"An actor's team assignment changed (authority). Payload FGM_TeamChangedPayload.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_GM_RespawnRequested, "DP.Bus.GM.RespawnRequested",
		"A respawn was requested and queued (authority). Payload FGM_RespawnedPayload.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_GM_Respawned, "DP.Bus.GM.Respawned",
		"An actor has been respawned (authority). Payload FGM_RespawnedPayload.");
}

//~ Module implementation --------------------------------------------------------------------------

class FDesignPatternsGameModeModule : public IModuleInterface
{
public:
	virtual void StartupModule() override { UE_LOG(LogDP, Log, TEXT("DesignPatternsGameMode module started.")); }
	virtual void ShutdownModule() override { UE_LOG(LogDP, Log, TEXT("DesignPatternsGameMode module shut down.")); }
};

IMPLEMENT_MODULE(FDesignPatternsGameModeModule, DesignPatternsGameMode)
