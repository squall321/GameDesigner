// Copyright DesignPatterns plugin. All Rights Reserved.

#include "DesignPatternsAudioModule.h"
#include "Core/DPLog.h"

#define LOCTEXT_NAMESPACE "FDesignPatternsAudioModule"

// Define the native anchor tags declared in DesignPatternsAudioModule.h. Defined here in the module's
// primary translation unit so the hierarchy is registered exactly once at load.
namespace AudioNativeTags
{
	UE_DEFINE_GAMEPLAY_TAG(Audio, "DP.Audio");
	UE_DEFINE_GAMEPLAY_TAG(Sound, "DP.Audio.Sound");
	UE_DEFINE_GAMEPLAY_TAG(Category, "DP.Audio.Category");
	UE_DEFINE_GAMEPLAY_TAG(Mix, "DP.Audio.Mix");
	UE_DEFINE_GAMEPLAY_TAG(Service_Audio, "DP.Service.Audio");
	UE_DEFINE_GAMEPLAY_TAG(Bus, "DP.Bus.Audio");
	UE_DEFINE_GAMEPLAY_TAG(Bus_Play, "DP.Bus.Audio.Play");
	UE_DEFINE_GAMEPLAY_TAG(Bus_Mix, "DP.Bus.Audio.Mix");
	UE_DEFINE_GAMEPLAY_TAG(Bus_CategoryVolume, "DP.Bus.Audio.CategoryVolume");

	// Additive deepening anchors.
	UE_DEFINE_GAMEPLAY_TAG(Service_AudioVO, "DP.Service.Audio.VO");
	UE_DEFINE_GAMEPLAY_TAG(VO, "DP.Audio.VO");
	UE_DEFINE_GAMEPLAY_TAG(Surface, "DP.Audio.Surface");
	UE_DEFINE_GAMEPLAY_TAG(MixDuck, "DP.Audio.Mix.Duck");

	// Mirror of DesignPatternsLocalization's DP.Bus.Loc.VoiceLine. Native gameplay-tag registration
	// is idempotent for identical names, so co-defining the same string in both modules is safe and
	// lets audio forward a caption payload without depending on the Localization module.
	UE_DEFINE_GAMEPLAY_TAG(Bus_LocVoiceLine, "DP.Bus.Loc.VoiceLine");
}

void FDesignPatternsAudioModule::StartupModule()
{
	UE_LOG(LogDP, Log, TEXT("DesignPatternsAudio module started."));
}

void FDesignPatternsAudioModule::ShutdownModule()
{
	UE_LOG(LogDP, Log, TEXT("DesignPatternsAudio module shut down."));
}

IMPLEMENT_MODULE(FDesignPatternsAudioModule, DesignPatternsAudio)

#undef LOCTEXT_NAMESPACE
