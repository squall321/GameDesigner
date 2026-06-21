// Copyright DesignPatterns plugin. All Rights Reserved.

#include "DesignPatternsTutorialModule.h"
#include "Modules/ModuleManager.h"
#include "Core/DPLog.h"

namespace TutTags
{
	// --- Service-locator keys (under the core DP.Service root so the locator lists them alongside others) ---

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Service_UIHighlight, "DP.Service.UI.Highlight",
		"Key under which a UI module publishes its ISeam_UIHighlight provider; resolved by the tutorial runner.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Service_InputModeArbiter, "DP.Service.Input.ModeArbiter",
		"Shared key under which the Platform module publishes the ISeam_InputModeArbiter; resolved to gate input.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Service_WorldHubQueryable, "DP.Service.World.HubQueryable",
		"Key under which a project publishes the IWorldHub_Queryable read seam; resolved by hint/condition eval.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Service_AnalyticsSink, "DP.Service.Analytics.Sink",
		"Key under which a project publishes the ISeam_AnalyticsSink; resolved to mirror completion/skip events.");

	// --- Message-bus channels this module broadcasts on (under the core DP.Bus root) ---

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_TutorialStarted, "DP.Bus.Tutorial.Started",
		"Broadcast when a tutorial begins; payload FTut_TutorialEvent.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_TutorialStepChanged, "DP.Bus.Tutorial.StepChanged",
		"Broadcast each time the active tutorial advances to a new step; payload FTut_TutorialEvent.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_TutorialCompleted, "DP.Bus.Tutorial.Completed",
		"Broadcast when a tutorial completes or is skipped; payload FTut_TutorialEvent.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_HintShown, "DP.Bus.Tutorial.HintShown",
		"Broadcast when a contextual hint is shown; payload FTut_HintEvent.");

	// --- Shared HUD contract channel (owned by the HUD module; this module references it by tag only) ---

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_HUD_Notify, "DP.Bus.HUD.Notify",
		"Shared HUD notification-request channel; the hint subsystem broadcasts here so the HUD surfaces a toast.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(HUD_Notify_Hint, "DP.HUD.Notify.Info",
		"Default HUD notification category applied to surfaced hints (the conventional Info category).");

	// --- Persistence ---

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Persist_Kind_Tutorial, "Tut.Persist.Kind.Completed",
		"ISeam_Persistable record-kind tag the tutorial subsystem captures/restores its completed set under.");

	// --- Analytics ---

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Analytics_TutorialCompleted, "Tut.Analytics.TutorialCompleted",
		"Analytics event recorded through ISeam_AnalyticsSink when a tutorial completes.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Analytics_TutorialSkipped, "Tut.Analytics.TutorialSkipped",
		"Analytics event recorded through ISeam_AnalyticsSink when a tutorial is skipped.");
}

/**
 * Module implementation for DesignPatternsTutorial. Pure lifecycle logging — all behaviour lives in the
 * tutorial/hint subsystems, the data assets, the conditions and the developer settings. The module owns no
 * runtime state of its own.
 */
void FDesignPatternsTutorialModule::StartupModule()
{
	UE_LOG(LogDP, Log, TEXT("DesignPatternsTutorial module started."));
}

void FDesignPatternsTutorialModule::ShutdownModule()
{
	UE_LOG(LogDP, Log, TEXT("DesignPatternsTutorial module shut down."));
}

IMPLEMENT_MODULE(FDesignPatternsTutorialModule, DesignPatternsTutorial)
