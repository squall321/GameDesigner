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

	// Additive deepening roots (crafting depth + building).
	UE_DEFINE_GAMEPLAY_TAG(Tech, "Surv.Tech");
	UE_DEFINE_GAMEPLAY_TAG(Build, "Surv.Build");
	UE_DEFINE_GAMEPLAY_TAG(BuildSocketType, "Surv.Build.SocketType");
	UE_DEFINE_GAMEPLAY_TAG(Quality, "Surv.Quality");

	// Bus channels — CHILDREN of the existing Survival bus anchor (DP.Bus.Survival.*).
	UE_DEFINE_GAMEPLAY_TAG(Bus_BuildPlaced, "DP.Bus.Survival.Build.Placed");
	UE_DEFINE_GAMEPLAY_TAG(Bus_BuildRemoved, "DP.Bus.Survival.Build.Removed");
	UE_DEFINE_GAMEPLAY_TAG(Bus_BuildSupportChanged, "DP.Bus.Survival.Build.SupportChanged");
	UE_DEFINE_GAMEPLAY_TAG(Bus_KnowledgeChanged, "DP.Bus.Survival.Knowledge.Changed");
	UE_DEFINE_GAMEPLAY_TAG(Bus_CraftCritical, "DP.Bus.Survival.Craft.Critical");

	// Persistence-kind tags.
	UE_DEFINE_GAMEPLAY_TAG(Persist_Knowledge, "Surv.Persist.Knowledge");
	UE_DEFINE_GAMEPLAY_TAG(Persist_Structure, "Surv.Persist.Structure");
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
