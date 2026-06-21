// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "NativeGameplayTags.h"

/**
 * DesignPatternsTutorial module interface.
 *
 * A data-driven onboarding layer split into two cooperating-but-independent areas:
 *  - TUTORIAL: a sequenced, message-bus-driven tutorial runner (UTut_TutorialSubsystem) that advances a
 *    UTut_TutorialDefinition's steps as trigger/completion conditions fire, surfaces the current step
 *    through a UTut_TutorialViewModel, highlights a UI target via ISeam_UIHighlight, optionally gates input
 *    via ISeam_InputModeArbiter, and persists completed tutorials via ISeam_Persistable so they never repeat.
 *  - HINT: a contextual, priority-queued, cooldowned hint system (UTut_HintSubsystem) driven by bus events
 *    and world-hub conditions, surfaced through the HUD notification channel by bus tag.
 *
 * Everything is LOCAL/COSMETIC and lives on the GameInstance: these subsystems never replicate state and
 * never mutate authoritative gameplay. They observe already-replicated state through the core message bus
 * (DP.Bus.*) and the IWorldHub_Queryable seam, and reach other modules ONLY through seams + bus tags.
 *
 * This module's native GameplayTags all live in TutTags (DesignPatternsTutorialModule.h / .cpp) — the
 * single tag registry for the module. New module-anchored tags are added there, not scattered across files.
 */
class FDesignPatternsTutorialModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

/**
 * Native anchor tags for the DesignPatternsTutorial module.
 *
 * These are the C++-defined roots/leaves the tutorial + hint code references literally at runtime:
 *  - service-locator keys under which the optional UI-highlight and input-mode-arbiter seams are published
 *    by other modules (this module only RESOLVES them — the providers register elsewhere);
 *  - the message-bus channels this module BROADCASTS on (tutorial started/step-changed/completed, a hint
 *    shown) so a HUD/UI can react without depending on this module;
 *  - the well-known HUD notification request channel this module surfaces hints onto (a shared cross-module
 *    contract tag owned by the HUD module — referenced by tag, never by including HUD's header);
 *  - the ISeam_Persistable record-kind tag the tutorial subsystem captures/restores its completed set under;
 *  - the analytics event tags the module records through ISeam_AnalyticsSink.
 *
 * Everything designer-facing (per-game tutorial/hint identities, highlight styles, input modes) flows through
 * data assets / settings as opaque FGameplayTag values and is NEVER hard-coded here. Full strings live in
 * DesignPatternsTutorialModule.cpp.
 */
namespace TutTags
{
	// --- Service-locator keys this module RESOLVES (providers registered by other modules) ---

	/** Key under which a UI implementation publishes its ISeam_UIHighlight provider. */
	DESIGNPATTERNSTUTORIAL_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_UIHighlight);

	/** Key under which the Platform module publishes the shared ISeam_InputModeArbiter provider. */
	DESIGNPATTERNSTUTORIAL_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_InputModeArbiter);

	/** Key under which a project publishes the IWorldHub_Queryable world-state read seam. */
	DESIGNPATTERNSTUTORIAL_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_WorldHubQueryable);

	/** Key under which a project publishes the ISeam_AnalyticsSink (optional, default-off). */
	DESIGNPATTERNSTUTORIAL_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_AnalyticsSink);

	// --- Message-bus channels this module BROADCASTS on (local/cosmetic; under the core DP.Bus root) ---

	/** Broadcast when a tutorial begins (payload FTut_TutorialEvent). */
	DESIGNPATTERNSTUTORIAL_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_TutorialStarted);

	/** Broadcast each time the active tutorial advances to a new step (payload FTut_TutorialEvent). */
	DESIGNPATTERNSTUTORIAL_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_TutorialStepChanged);

	/** Broadcast when a tutorial completes or is skipped (payload FTut_TutorialEvent). */
	DESIGNPATTERNSTUTORIAL_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_TutorialCompleted);

	/** Broadcast when a contextual hint is shown (payload FTut_HintEvent), for analytics/telemetry mirrors. */
	DESIGNPATTERNSTUTORIAL_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_HintShown);

	// --- Shared cross-module HUD contract channel (owned by the HUD module; referenced by tag only) ---

	/**
	 * The HUD notification-request channel (DP.Bus.HUD.Notify). The hint subsystem broadcasts a notification
	 * request on this channel so the HUD module's notification queue surfaces the hint as a toast — without
	 * this module ever including the HUD module's concrete header. If no HUD module listens, the broadcast is
	 * an inert no-op (degrades gracefully).
	 */
	DESIGNPATTERNSTUTORIAL_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_HUD_Notify);

	/** Default HUD notification category applied to hints when a hint definition leaves it unset. */
	DESIGNPATTERNSTUTORIAL_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(HUD_Notify_Hint);

	// --- Persistence ---

	/** ISeam_Persistable record-kind tag the tutorial subsystem captures/restores its completed set under. */
	DESIGNPATTERNSTUTORIAL_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Persist_Kind_Tutorial);

	// --- Analytics event tags (recorded through ISeam_AnalyticsSink when present) ---

	/** Analytics event: a tutorial was completed (attrs carry the tutorial DataTag + step count). */
	DESIGNPATTERNSTUTORIAL_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Analytics_TutorialCompleted);

	/** Analytics event: a tutorial was skipped (attrs carry the tutorial DataTag + reached step). */
	DESIGNPATTERNSTUTORIAL_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Analytics_TutorialSkipped);
}
