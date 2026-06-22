// Copyright DesignPatterns plugin. All Rights Reserved.

#include "DesignPatternsGameFlowModule.h"
#include "Modules/ModuleManager.h"
#include "Core/DPLog.h"

namespace FlowTags
{
	// --- Canonical top-level flow phase tags under the Flow.Phase root ---
	UE_DEFINE_GAMEPLAY_TAG(Phase_Boot,     "Flow.Phase.Boot");
	UE_DEFINE_GAMEPLAY_TAG(Phase_Title,    "Flow.Phase.Title");
	UE_DEFINE_GAMEPLAY_TAG(Phase_MainMenu, "Flow.Phase.MainMenu");
	UE_DEFINE_GAMEPLAY_TAG(Phase_Lobby,    "Flow.Phase.Lobby");
	UE_DEFINE_GAMEPLAY_TAG(Phase_Loading,  "Flow.Phase.Loading");
	UE_DEFINE_GAMEPLAY_TAG(Phase_InGame,   "Flow.Phase.InGame");
	UE_DEFINE_GAMEPLAY_TAG(Phase_Pause,    "Flow.Phase.Pause");
	UE_DEFINE_GAMEPLAY_TAG(Phase_Results,  "Flow.Phase.Results");
	UE_DEFINE_GAMEPLAY_TAG(Phase_NetError, "Flow.Phase.NetError");
	UE_DEFINE_GAMEPLAY_TAG(Phase_Splash,   "Flow.Phase.Splash");

	// --- Service-locator keys under the core DP.Service root ---
	UE_DEFINE_GAMEPLAY_TAG(Service_AppFlowController, "DP.Service.Flow.AppFlowController");
	UE_DEFINE_GAMEPLAY_TAG(Service_InputModeArbiter,  "DP.Service.Input.ModeArbiter");
	UE_DEFINE_GAMEPLAY_TAG(Service_SaveSlotManager,   "DP.Service.Save.SlotManager");
	UE_DEFINE_GAMEPLAY_TAG(Service_ScoreSource,       "DP.Service.Score.Source");
	UE_DEFINE_GAMEPLAY_TAG(Service_NetSession,        "DP.Service.Net.Session");
	UE_DEFINE_GAMEPLAY_TAG(Service_LobbyRead,         "DP.Service.Net.LobbyRead");
	UE_DEFINE_GAMEPLAY_TAG(Service_StreamingControl,  "DP.Service.Streaming.Control");
	UE_DEFINE_GAMEPLAY_TAG(Service_AppLifecycle,      "DP.Service.Platform.AppLifecycle");
	UE_DEFINE_GAMEPLAY_TAG(Service_FlowGuard,         "DP.Service.Flow.Guard");

	// --- Input-mode identities under the shared DP.Input.Mode root ---
	UE_DEFINE_GAMEPLAY_TAG(InputMode_Menu,  "DP.Input.Mode.Menu");
	UE_DEFINE_GAMEPLAY_TAG(InputMode_Game,  "DP.Input.Mode.Game");
	UE_DEFINE_GAMEPLAY_TAG(InputMode_Pause, "DP.Input.Mode.Pause");

	// --- Message-bus channels under the core DP.Bus root ---
	UE_DEFINE_GAMEPLAY_TAG(Bus_PhaseChanged,    "DP.Bus.Flow.PhaseChanged");
	UE_DEFINE_GAMEPLAY_TAG(Bus_ScreenPush,      "DP.Bus.Flow.Screen.Push");
	UE_DEFINE_GAMEPLAY_TAG(Bus_ScreenPop,       "DP.Bus.Flow.Screen.Pop");
	UE_DEFINE_GAMEPLAY_TAG(Bus_LoadingProgress, "DP.Bus.Flow.LoadingProgress");
	UE_DEFINE_GAMEPLAY_TAG(Bus_MatchmakingChanged, "DP.Bus.Flow.MatchmakingChanged");
	UE_DEFINE_GAMEPLAY_TAG(Bus_TravelStarted,      "DP.Bus.Flow.TravelStarted");
	UE_DEFINE_GAMEPLAY_TAG(Bus_TravelFailed,       "DP.Bus.Flow.TravelFailed");
	UE_DEFINE_GAMEPLAY_TAG(Bus_BootStepChanged,    "DP.Bus.Flow.BootStepChanged");
	UE_DEFINE_GAMEPLAY_TAG(Bus_AutoSaveHint,       "DP.Bus.Flow.AutoSaveHint");

	// --- Boot-step kind tags under the Flow.BootStep root ---
	UE_DEFINE_GAMEPLAY_TAG(BootStep_Legal,       "Flow.BootStep.Legal");
	UE_DEFINE_GAMEPLAY_TAG(BootStep_Preload,     "Flow.BootStep.Preload");
	UE_DEFINE_GAMEPLAY_TAG(BootStep_ProfileLoad, "Flow.BootStep.ProfileLoad");
	UE_DEFINE_GAMEPLAY_TAG(BootStep_FirstRun,    "Flow.BootStep.FirstRun");

	// --- Flow-guard deny reasons under the Flow.Guard.Reason root ---
	UE_DEFINE_GAMEPLAY_TAG(GuardReason_NoProfile, "Flow.Guard.Reason.NoProfile");

	// --- Matchmaking phase tags under the Flow.Matchmaking.Phase root ---
	UE_DEFINE_GAMEPLAY_TAG(MMPhase_Idle,       "Flow.Matchmaking.Phase.Idle");
	UE_DEFINE_GAMEPLAY_TAG(MMPhase_Searching,  "Flow.Matchmaking.Phase.Searching");
	UE_DEFINE_GAMEPLAY_TAG(MMPhase_Connecting, "Flow.Matchmaking.Phase.Connecting");
	UE_DEFINE_GAMEPLAY_TAG(MMPhase_Active,     "Flow.Matchmaking.Phase.Active");
	UE_DEFINE_GAMEPLAY_TAG(MMPhase_Failed,     "Flow.Matchmaking.Phase.Failed");
}

void FDesignPatternsGameFlowModule::StartupModule()
{
	UE_LOG(LogDP, Log, TEXT("DesignPatternsGameFlow module started."));
}

void FDesignPatternsGameFlowModule::ShutdownModule()
{
	UE_LOG(LogDP, Log, TEXT("DesignPatternsGameFlow module shut down."));
}

IMPLEMENT_MODULE(FDesignPatternsGameFlowModule, DesignPatternsGameFlow)
