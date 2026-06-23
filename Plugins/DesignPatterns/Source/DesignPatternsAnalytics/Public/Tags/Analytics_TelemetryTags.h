// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

/**
 * Native GameplayTags owned by the deepening "telemetry" area of DesignPatternsAnalytics
 * (funnel/cohort, heatmap, economy, performance, breadcrumb, debug dashboard).
 *
 * Design intent (mirrors AnalyticsNativeTags / AnalyticsProgressionTags):
 *  - These are this module's OWN analytics-event ids (Analytics.Event.*) and bus-channel /
 *    service-locator keys. They live under the module's own roots so the module never
 *    compile-depends on another module's tag table.
 *  - Every emission funnels through the consent-gated UAnalytics_Subsystem::RecordEvent, so
 *    these tags identify what is *chosen to be measured*, never a privacy-bearing value.
 *
 * Full tag strings are defined in Analytics_TelemetryTags.cpp.
 */
namespace AnalyticsTelemetryTags
{
	// ---- Funnel / cohort events (Analytics.Event.Funnel.*) ----

	/** Recorded when a named funnel run begins (BeginFunnel). */
	DESIGNPATTERNSANALYTICS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Funnel_Begin);

	/** Recorded each time a funnel advances to a new (deeper) step. */
	DESIGNPATTERNSANALYTICS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Funnel_Step);

	/** Recorded when a funnel completes, abandons (timeout) or is summarised. */
	DESIGNPATTERNSANALYTICS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Funnel_Summary);

	// ---- Heatmap events (Analytics.Event.Heatmap.*) ----

	/** Aggregate heatmap export marker (carries category + non-empty bucket count). */
	DESIGNPATTERNSANALYTICS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Heatmap_Export);

	// ---- Economy events (Analytics.Event.Economy.*) ----

	/** Recorded by the economy-telemetry component for a single resource flow (sink/source). */
	DESIGNPATTERNSANALYTICS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Economy_Flow);

	/** Aggregate economy snapshot (net flow per resource) emitted on demand / teardown. */
	DESIGNPATTERNSANALYTICS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Economy_Snapshot);

	// ---- Performance events (Analytics.Event.Perf.*) ----

	/** Periodic performance sample summary (percentile frame times + hitch count). */
	DESIGNPATTERNSANALYTICS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Perf_Sample);

	/** Recorded when a frame exceeds the configured hitch threshold. */
	DESIGNPATTERNSANALYTICS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Perf_Hitch);

	// ---- Crash / breadcrumb events (Analytics.Event.Crash.*) ----

	/** Recorded when the breadcrumb trail is attached to an error/crash report. */
	DESIGNPATTERNSANALYTICS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Crash_BreadcrumbAttached);

	// ---- Cohort dimensions (Analytics.Cohort.*) — attribute VALUES, never PII ----

	/** Default cohort dimension key under which a host-pushed install-week / source cohort is filed. */
	DESIGNPATTERNSANALYTICS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cohort_Dimension_Default);

	// ---- Default service / bus keys (fallback seeds; real values are settings) ----

	/**
	 * Default bus subtree the breadcrumb + dashboard subsystems observe when the project has not
	 * overridden the corresponding setting. A documented DEFAULT seed only — not a compile coupling.
	 */
	DESIGNPATTERNSANALYTICS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DefaultTelemetryBusChannel);
}
