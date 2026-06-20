// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

/**
 * Native (C++-defined) GameplayTags owned by the DesignPatternsAnalytics module.
 *
 * Design intent:
 *  - The module owns its OWN analytics-event root (Analytics.Event.*). These are the
 *    canonical event ids handed to the ISeam_AnalyticsSink. They are deliberately NOT
 *    anchored under the core DP.Bus root: a bus channel is "something happened in the
 *    game", an analytics event is "something we chose to measure". Keeping them separate
 *    lets a project rename/relocate its bus channels without disturbing telemetry ids.
 *
 *  - The module does NOT hard-code which bus channel it listens on. The subscribe channel
 *    is a designer-configurable FGameplayTag setting (Analytics_DeveloperSettings::
 *    BusChannelToObserve) so a project can point analytics at any subtree of its bus
 *    (e.g. "DP.Bus") WITHOUT this module taking a compile dependency on DP.Bus or on any
 *    other Wave-2/Wave-1 module's tags. We expose a documented default channel constant
 *    here purely as the fallback the settings CDO seeds itself with.
 *
 * The full tag strings are defined in DesignPatternsAnalyticsModule.cpp.
 */
namespace AnalyticsNativeTags
{
	// ---- Own analytics-event root (Analytics.Event.*) ----

	/** Root of all analytics events emitted by this module. Children are concrete events. */
	DESIGNPATTERNSANALYTICS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Analytics_Event);

	/**
	 * Generic event recorded when a bus message arrives on the observed channel but the
	 * event-map asset has no explicit mapping for that channel. Acts as a catch-all so an
	 * unmapped-but-interesting signal is still measurable (the original channel is carried
	 * as a "channel" name attribute). Projects can suppress this in the event map.
	 */
	DESIGNPATTERNSANALYTICS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Analytics_Event_BusUnmapped);

	/** Session lifecycle: recorded once when consent is granted and recording begins. */
	DESIGNPATTERNSANALYTICS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Analytics_Event_SessionStart);

	/** Session lifecycle: recorded on a clean flush at subsystem shutdown. */
	DESIGNPATTERNSANALYTICS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Analytics_Event_SessionEnd);

	// ---- Default observed bus channel (fallback only; the real value is a setting) ----

	/**
	 * The default bus subtree the subsystem observes when the project has not overridden
	 * Analytics_DeveloperSettings::BusChannelToObserve. Chosen as the core bus root so an
	 * out-of-the-box install measures everything; this is a DEFAULT seed, not a compile
	 * coupling — the module never references another module's concrete channel tags.
	 */
	DESIGNPATTERNSANALYTICS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(DefaultObservedBusChannel);
}
