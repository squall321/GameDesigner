// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Tags/Analytics_TelemetryTags.h"

namespace AnalyticsTelemetryTags
{
	// ---- Funnel / cohort ----
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Funnel_Begin, "Analytics.Event.Funnel.Begin",
		"Recorded when a named GI-wide funnel run begins.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Funnel_Step, "Analytics.Event.Funnel.Step",
		"Recorded each time a funnel advances to a new, deeper step.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Funnel_Summary, "Analytics.Event.Funnel.Summary",
		"Recorded when a funnel completes, abandons (timeout) or is summarised on shutdown.");

	// ---- Heatmap ----
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Heatmap_Export, "Analytics.Event.Heatmap.Export",
		"Aggregate heatmap export marker carrying category and non-empty bucket count.");

	// ---- Economy ----
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Economy_Flow, "Analytics.Event.Economy.Flow",
		"A single resource flow (sink/source) recorded by the economy-telemetry component.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Economy_Snapshot, "Analytics.Event.Economy.Snapshot",
		"Aggregate economy snapshot: net flow per resource, emitted on demand or teardown.");

	// ---- Performance ----
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Perf_Sample, "Analytics.Event.Perf.Sample",
		"Periodic performance sample summary: percentile frame times and hitch count.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Perf_Hitch, "Analytics.Event.Perf.Hitch",
		"Recorded when a frame exceeds the configured hitch threshold.");

	// ---- Crash / breadcrumb ----
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Event_Crash_BreadcrumbAttached, "Analytics.Event.Crash.BreadcrumbAttached",
		"Recorded when the breadcrumb trail is attached to an error/crash report.");

	// ---- Cohort dimensions ----
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Cohort_Dimension_Default, "Analytics.Cohort.Dimension.Default",
		"Default cohort dimension key for a host-pushed install-week / acquisition-source cohort.");

	// ---- Default service / bus seeds ----
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(DefaultTelemetryBusChannel, "DP.Bus",
		"Default bus subtree the telemetry breadcrumb/dashboard observe when no project override is set.");
}
