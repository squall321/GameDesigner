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
