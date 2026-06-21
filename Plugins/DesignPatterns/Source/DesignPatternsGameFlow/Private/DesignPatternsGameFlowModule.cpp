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

	// --- Service-locator keys under the core DP.Service root ---
	UE_DEFINE_GAMEPLAY_TAG(Service_AppFlowController, "DP.Service.Flow.AppFlowController");
	UE_DEFINE_GAMEPLAY_TAG(Service_InputModeArbiter,  "DP.Service.Input.ModeArbiter");
	UE_DEFINE_GAMEPLAY_TAG(Service_SaveSlotManager,   "DP.Service.Save.SlotManager");
	UE_DEFINE_GAMEPLAY_TAG(Service_ScoreSource,       "DP.Service.Score.Source");

	// --- Input-mode identities under the shared DP.Input.Mode root ---
	UE_DEFINE_GAMEPLAY_TAG(InputMode_Menu,  "DP.Input.Mode.Menu");
	UE_DEFINE_GAMEPLAY_TAG(InputMode_Game,  "DP.Input.Mode.Game");
	UE_DEFINE_GAMEPLAY_TAG(InputMode_Pause, "DP.Input.Mode.Pause");

	// --- Message-bus channels under the core DP.Bus root ---
	UE_DEFINE_GAMEPLAY_TAG(Bus_PhaseChanged,    "DP.Bus.Flow.PhaseChanged");
	UE_DEFINE_GAMEPLAY_TAG(Bus_ScreenPush,      "DP.Bus.Flow.Screen.Push");
	UE_DEFINE_GAMEPLAY_TAG(Bus_ScreenPop,       "DP.Bus.Flow.Screen.Pop");
	UE_DEFINE_GAMEPLAY_TAG(Bus_LoadingProgress, "DP.Bus.Flow.LoadingProgress");
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
