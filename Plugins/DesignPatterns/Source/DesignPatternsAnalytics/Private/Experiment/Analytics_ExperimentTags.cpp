// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Experiment/Analytics_ExperimentTags.h"

namespace AnalyticsProgressionTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Experiment_Assigned, "Analytics.Event.Experiment.Assigned",
		"Player was bucketed into an experiment variant for the first time (sticky assignment).");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Progression_FunnelStep, "Analytics.Event.Progression.FunnelStep",
		"A progression funnel step was reached for the first time this session.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Progression_Milestone, "Analytics.Event.Progression.Milestone",
		"A configured progression milestone was reached.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Progression_PlaytimeHeartbeat, "Analytics.Event.Progression.PlaytimeHeartbeat",
		"Periodic accumulated-playtime heartbeat for session-length analysis.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Session_Summary, "Analytics.Event.Session.Summary",
		"Session-summary event emitted on app suspend or clean shutdown.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_App_Suspend, "Analytics.Bus.App.Suspend",
		"Bus channel carrying the OS app-suspend/background signal, bridged by the host/Platform module.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_App_Resume, "Analytics.Bus.App.Resume",
		"Bus channel carrying the OS app-resume/foreground signal.");
}
