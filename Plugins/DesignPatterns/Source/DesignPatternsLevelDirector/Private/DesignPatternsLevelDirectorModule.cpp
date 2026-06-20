// Copyright DesignPatterns plugin. All Rights Reserved.

#include "DesignPatternsLevelDirectorModule.h"
#include "Core/DPLog.h"

namespace LvlTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Service_StreamingDirector, "DP.Service.Lvl.StreamingDirector",
		"Service-locator key for the level-director streaming subsystem (ULvl_StreamingDirectorSubsystem).");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Service_SpawnRegionProvider, "DP.Service.Lvl.SpawnRegionProvider",
		"Service-locator key under which spawn-region providers register (ILvl_SpawnRegionProvider).");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Service_AnalyticsSink, "DP.Service.Lvl.AnalyticsSink",
		"Service key the streaming director resolves an ISeam_AnalyticsSink under. No-op when unresolved.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_StreamingLevelLoading, "DP.Bus.Lvl.StreamingLevelLoading",
		"Broadcast when a streaming level/cell begins loading.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_StreamingLevelLoaded, "DP.Bus.Lvl.StreamingLevelLoaded",
		"Broadcast when a streaming level/cell has finished loading and is visible.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_StreamingLevelUnloading, "DP.Bus.Lvl.StreamingLevelUnloading",
		"Broadcast when a streaming level/cell begins unloading.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Gate_StreamingEnabled, "DP.Gate.Lvl.StreamingEnabled",
		"Activation gate that, when closed, suspends the streaming director. Defaults OPEN when unresolved.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Analytics_StreamingChurn, "DP.Analytics.Lvl.StreamingChurn",
		"Aggregate analytics event summarizing streaming load/unload churn over a reporting interval.");
}

void FDesignPatternsLevelDirectorModule::StartupModule()
{
	UE_LOG(LogDP, Log, TEXT("DesignPatternsLevelDirector module started."));
}

void FDesignPatternsLevelDirectorModule::ShutdownModule()
{
	UE_LOG(LogDP, Log, TEXT("DesignPatternsLevelDirector module shut down."));
}

IMPLEMENT_MODULE(FDesignPatternsLevelDirectorModule, DesignPatternsLevelDirector)
