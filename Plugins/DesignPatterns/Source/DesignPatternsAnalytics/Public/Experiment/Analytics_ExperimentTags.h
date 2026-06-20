// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

/**
 * Native GameplayTags owned by the progression / experiment area of DesignPatternsAnalytics.
 *
 * These are analytics-event ids and service-locator keys this area both DEFINES and consumes.
 * They live under the module's own "Analytics.*" roots so the module never compile-depends on
 * another module's tag table. Full strings are defined in Analytics_ExperimentTags.cpp.
 */
namespace AnalyticsProgressionTags
{
	// ---- Experiment / feature-flag events (Analytics.Event.Experiment.*) ----

	/** Recorded the first time a player is assigned to a variant of a given experiment (sticky). */
	DESIGNPATTERNSANALYTICS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Experiment_Assigned);

	// ---- Progression / funnel events (Analytics.Event.Progression.*) ----

	/** Recorded by UAnalytics_ProgressionComponent when a funnel step is first reached. */
	DESIGNPATTERNSANALYTICS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Progression_FunnelStep);

	/** Recorded when a configured milestone is reached (OnMilestoneReached also fires). */
	DESIGNPATTERNSANALYTICS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Progression_Milestone);

	/** Periodic / on-demand playtime heartbeat carrying accumulated session playtime seconds. */
	DESIGNPATTERNSANALYTICS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Progression_PlaytimeHeartbeat);

	/** Session-summary event emitted by the session tracker on app suspend / clean shutdown. */
	DESIGNPATTERNSANALYTICS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Session_Summary);

	// NOTE: the service-locator keys for the player-id provider and the analytics sink are owned by
	// the core area's Analytics_DeveloperSettings (PlayerIdProviderServiceTag / AnalyticsSinkServiceTag),
	// so this area reads them from settings rather than declaring its own duplicate service tags.

	// ---- Bus channel this area listens on for the platform suspend bridge ----

	/**
	 * Bus channel the session tracker subscribes to for an app-suspend signal. The Platform
	 * module (or the host) re-broadcasts the OS suspend/background event onto this channel; we
	 * subscribe by tag and NEVER take a compile dependency on the Platform module.
	 */
	DESIGNPATTERNSANALYTICS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_App_Suspend);

	/** Companion resume signal; the tracker uses it to start a fresh session segment. */
	DESIGNPATTERNSANALYTICS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_App_Resume);
}
