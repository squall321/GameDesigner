// Copyright DesignPatterns plugin. All Rights Reserved.

#include "DesignPatternsAnalyticsModule.h"
#include "Modules/ModuleManager.h"
#include "Core/DPLog.h"

namespace AnalyticsNativeTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Analytics_Event, "Analytics.Event",
		"Root of all analytics events emitted by the DesignPatternsAnalytics module.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Analytics_Event_BusUnmapped, "Analytics.Event.BusUnmapped",
		"Catch-all event for an observed bus message that the event map did not explicitly map.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Analytics_Event_SessionStart, "Analytics.Event.SessionStart",
		"Recorded once when analytics consent is granted and recording begins.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Analytics_Event_SessionEnd, "Analytics.Event.SessionEnd",
		"Recorded on a clean flush at analytics subsystem shutdown.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(DefaultObservedBusChannel, "DP.Bus",
		"Default bus subtree the analytics subsystem observes when no project override is set.");
}

/**
 * Module implementation for DesignPatternsAnalytics. Pure lifecycle logging; all behaviour
 * lives in the developer settings, the event-map data asset and the analytics subsystem.
 */
class FDesignPatternsAnalyticsModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		UE_LOG(LogDP, Log, TEXT("DesignPatternsAnalytics module started."));
	}

	virtual void ShutdownModule() override
	{
		UE_LOG(LogDP, Log, TEXT("DesignPatternsAnalytics module shut down."));
	}
};

IMPLEMENT_MODULE(FDesignPatternsAnalyticsModule, DesignPatternsAnalytics)
