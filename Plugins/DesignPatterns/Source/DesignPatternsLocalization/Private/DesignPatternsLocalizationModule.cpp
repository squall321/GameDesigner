// Copyright DesignPatterns plugin. All Rights Reserved.

#include "DesignPatternsLocalizationModule.h"
#include "Modules/ModuleManager.h"
#include "Core/DPLog.h"

namespace DPLocTags
{
	// Service-locator keys under the core DP.Service root so the locator lists them alongside every
	// other published provider. The localization key is owned by this module; the accessibility and
	// TTS keys are shared contract keys this module only resolves.
	UE_DEFINE_GAMEPLAY_TAG(Service_Localization,          "DP.Service.Loc.Localization");
	UE_DEFINE_GAMEPLAY_TAG(Service_AccessibilityProvider, "DP.Service.Loc.AccessibilityProvider");
	UE_DEFINE_GAMEPLAY_TAG(Service_TextToSpeech,          "DP.Service.Loc.TextToSpeech");

	// Message-bus channels under the core DP.Bus root.
	UE_DEFINE_GAMEPLAY_TAG(Bus_DialogueLine,    "DP.Bus.Loc.DialogueLine");
	UE_DEFINE_GAMEPLAY_TAG(Bus_VoiceLine,       "DP.Bus.Loc.VoiceLine");
	UE_DEFINE_GAMEPLAY_TAG(Bus_SubtitleShow,    "DP.Bus.Loc.Subtitle.Show");
	UE_DEFINE_GAMEPLAY_TAG(Bus_SubtitleClear,   "DP.Bus.Loc.Subtitle.Clear");
	UE_DEFINE_GAMEPLAY_TAG(Bus_SubtitleChanged, "DP.Bus.Loc.Subtitle.Changed");

	// TTS routing categories under this module's DP.Loc.TTS root.
	UE_DEFINE_GAMEPLAY_TAG(TTS_Subtitle, "DP.Loc.TTS.Subtitle");

	// Subtitle priority anchors under this module's DP.Loc.Subtitle.Priority root.
	UE_DEFINE_GAMEPLAY_TAG(SubtitlePriority_Ambient,  "DP.Loc.Subtitle.Priority.Ambient");
	UE_DEFINE_GAMEPLAY_TAG(SubtitlePriority_Dialogue, "DP.Loc.Subtitle.Priority.Dialogue");
	UE_DEFINE_GAMEPLAY_TAG(SubtitlePriority_Critical, "DP.Loc.Subtitle.Priority.Critical");
}

void FDesignPatternsLocalizationModule::StartupModule()
{
	UE_LOG(LogDP, Log, TEXT("DesignPatternsLocalization module started."));
}

void FDesignPatternsLocalizationModule::ShutdownModule()
{
	UE_LOG(LogDP, Log, TEXT("DesignPatternsLocalization module shut down."));
}

IMPLEMENT_MODULE(FDesignPatternsLocalizationModule, DesignPatternsLocalization)
