// Copyright DesignPatterns plugin. All Rights Reserved.

#include "DesignPatternsSurvivalModule.h"
#include "DesignPatternsSurvivalTags.h"
#include "Core/DPLog.h"

#define LOCTEXT_NAMESPACE "FDesignPatternsSurvivalModule"

// Define the native anchor tags declared in DesignPatternsSurvivalTags.h.
namespace SurvNativeTags
{
	UE_DEFINE_GAMEPLAY_TAG(Resource, "Surv.Resource");
	UE_DEFINE_GAMEPLAY_TAG(Need, "Surv.Need");
	UE_DEFINE_GAMEPLAY_TAG(Station, "Surv.Station");
	UE_DEFINE_GAMEPLAY_TAG(Bus, "DP.Bus.Survival");
}

void FDesignPatternsSurvivalModule::StartupModule()
{
	UE_LOG(LogDP, Log, TEXT("DesignPatternsSurvival module started."));
}

void FDesignPatternsSurvivalModule::ShutdownModule()
{
	UE_LOG(LogDP, Log, TEXT("DesignPatternsSurvival module shut down."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FDesignPatternsSurvivalModule, DesignPatternsSurvival)
