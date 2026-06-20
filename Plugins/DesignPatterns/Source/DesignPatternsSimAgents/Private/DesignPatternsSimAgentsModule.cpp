// Copyright DesignPatterns plugin. All Rights Reserved.

#include "DesignPatternsSimAgentsModule.h"
#include "DesignPatternsSimAgentsTags.h"
#include "Core/DPLog.h"

#define LOCTEXT_NAMESPACE "FDesignPatternsSimAgentsModule"

// Define the native anchor tags declared in DesignPatternsSimAgentsTags.h. These are defined here
// (in the module's primary translation unit) so the hierarchy is registered exactly once at load.
namespace SimAgNativeTags
{
	UE_DEFINE_GAMEPLAY_TAG(Need, "SimAg.Need");
	UE_DEFINE_GAMEPLAY_TAG(Activity, "SimAg.Activity");
	UE_DEFINE_GAMEPLAY_TAG(Location, "SimAg.Location");
	UE_DEFINE_GAMEPLAY_TAG(Service, "DP.Service.SimAgents");
	UE_DEFINE_GAMEPLAY_TAG(Service_Clock, "DP.Service.SimAgents.Clock");
	UE_DEFINE_GAMEPLAY_TAG(Service_JobBoard, "DP.Service.SimAgents.JobBoard");
	UE_DEFINE_GAMEPLAY_TAG(Service_FlowField, "DP.Service.SimAgents.FlowField");
	UE_DEFINE_GAMEPLAY_TAG(Persist, "DP.Persist.SimAgents");
	UE_DEFINE_GAMEPLAY_TAG(Persist_JobBoard, "DP.Persist.SimAgents.JobBoard");
	UE_DEFINE_GAMEPLAY_TAG(Persist_Agent, "DP.Persist.SimAgents.Agent");
	UE_DEFINE_GAMEPLAY_TAG(Bus, "DP.Bus.SimAgents");
	UE_DEFINE_GAMEPLAY_TAG(Bus_NeedCritical, "DP.Bus.SimAgents.NeedCritical");
	UE_DEFINE_GAMEPLAY_TAG(Bus_ActivityChanged, "DP.Bus.SimAgents.ActivityChanged");
	UE_DEFINE_GAMEPLAY_TAG(Bus_JobChanged, "DP.Bus.SimAgents.JobChanged");
}

void FDesignPatternsSimAgentsModule::StartupModule()
{
	UE_LOG(LogDP, Log, TEXT("DesignPatternsSimAgents module started."));
}

void FDesignPatternsSimAgentsModule::ShutdownModule()
{
	UE_LOG(LogDP, Log, TEXT("DesignPatternsSimAgents module shut down."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FDesignPatternsSimAgentsModule, DesignPatternsSimAgents)
