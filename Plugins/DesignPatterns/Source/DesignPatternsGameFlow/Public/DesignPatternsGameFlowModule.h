// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "NativeGameplayTags.h"

/**
 * DesignPatternsGameFlow module interface.
 *
 * Top-level application/game flow framework: a tag-keyed flow FSM over the canonical phases
 * (Boot/Title/MainMenu/Lobby/Loading/InGame/Pause/Results) that drives level travel, pushes the
 * phase's screen through the UI mediator (by message bus), sets the input mode through the shared
 * ISeam_InputModeArbiter, and remembers a "continue" target via ISeam_SaveSlotManager. It implements
 * and registers ISeam_AppFlowController so tutorial/AI-director/save-UI modules can read and drive
 * the phase without depending on this module.
 *
 * This module also owns the loading-screen wrapper (engine MoviePlayer + PreLoadMap/PostLoadMap),
 * the loading-progress ViewModel and the match-results ViewModel (which reads ISeam_ScoreSource).
 *
 * The module's native GameplayTags live in this header's FlowTags namespace (the single tag registry
 * for the whole GameFlow module): the canonical Flow.Phase.* tags, the service-locator keys this
 * module publishes/resolves, the input-mode tags it pushes, and the bus channels it broadcasts on.
 */
namespace FlowTags
{
	// --- Canonical top-level flow phase tags (Flow.Phase.*) ---
	// The flow FSM is keyed by these. Games may add child phases freely; this module only references
	// the canonical set its own code mentions by name. A per-phase UFlow_FlowStateDefinition data asset
	// (resolved by tag) supplies the level, screen, input-mode and allowed transitions for each.

	/** Earliest phase: engine/game just booted, before any front-end is shown. */
	DESIGNPATTERNSGAMEFLOW_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Phase_Boot);

	/** Title / attract screen (press-any-key). */
	DESIGNPATTERNSGAMEFLOW_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Phase_Title);

	/** Main menu front-end (new game / continue / settings). */
	DESIGNPATTERNSGAMEFLOW_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Phase_MainMenu);

	/** Multiplayer lobby / party screen before a match. */
	DESIGNPATTERNSGAMEFLOW_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Phase_Lobby);

	/** Transient loading phase while a target map streams in. */
	DESIGNPATTERNSGAMEFLOW_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Phase_Loading);

	/** Active gameplay. */
	DESIGNPATTERNSGAMEFLOW_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Phase_InGame);

	/** In-game pause overlay (game paused, pause screen pushed). */
	DESIGNPATTERNSGAMEFLOW_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Phase_Pause);

	/** Post-match results / scoreboard screen. */
	DESIGNPATTERNSGAMEFLOW_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Phase_Results);

	/**
	 * Hard error-recovery phase entered (via ForceTransition) on a terminal matchmaking failure or a
	 * travel/connection failure. Shows a NetError overlay; GoBack returns to Main Menu. Additive — no
	 * existing phase touched.
	 */
	DESIGNPATTERNSGAMEFLOW_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Phase_NetError);

	/** Optional early splash phase before Boot's data-driven sequence (legal/logo). Designer-opt-in. */
	DESIGNPATTERNSGAMEFLOW_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Phase_Splash);

	// --- Service-locator keys (published / resolved by this module) ---

	/** Key under which this module publishes its ISeam_AppFlowController (the flow subsystem). */
	DESIGNPATTERNSGAMEFLOW_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_AppFlowController);

	/**
	 * Key the shared ISeam_InputModeArbiter is published under (by the Platform module). The flow
	 * subsystem resolves a TScriptInterface<ISeam_InputModeArbiter> from this key to push/pop the
	 * per-phase input mode. Shared cross-module contract tag; the provider is registered elsewhere.
	 */
	DESIGNPATTERNSGAMEFLOW_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_InputModeArbiter);

	/**
	 * Key the ISeam_SaveSlotManager is published under (by the SaveSystem module). The flow subsystem
	 * resolves it to compute the "continue" target slot. Provider registered elsewhere.
	 */
	DESIGNPATTERNSGAMEFLOW_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_SaveSlotManager);

	/**
	 * Key the ISeam_ScoreSource is published under (by the GameMode/score module). The results
	 * ViewModel resolves it to read final scores. Provider registered elsewhere.
	 */
	DESIGNPATTERNSGAMEFLOW_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_ScoreSource);

	/**
	 * Key the ISeam_NetSession adapter is published under (by the Net module). The matchmaking controller
	 * resolves it to drive search/create/join through the seam. Provider registered elsewhere.
	 */
	DESIGNPATTERNSGAMEFLOW_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_NetSession);

	/**
	 * Key the ISeam_LobbyRead carrier is published under (by the Net module). The matchmaking controller
	 * reads readiness to advance Lobby -> Loading. Provider registered elsewhere.
	 */
	DESIGNPATTERNSGAMEFLOW_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_LobbyRead);

	/**
	 * Key the ISeam_StreamingControl adapter is published under (by the LevelDirector module). The
	 * streaming load coordinator RE-RESOLVES it per load (never cached across travel). Provider elsewhere.
	 */
	DESIGNPATTERNSGAMEFLOW_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_StreamingControl);

	/**
	 * Key the ISeam_AppLifecycle adapter is published under (by the Platform module). The pause controller
	 * registers a listener for OS suspend/resume. Provider registered elsewhere.
	 */
	DESIGNPATTERNSGAMEFLOW_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_AppLifecycle);

	/**
	 * Key under which ISeam_FlowGuard providers are published. The flow consults EVERY registered guard
	 * on a validated (non-forced) transition. This module registers its built-in UFlow_ProfileLoadedGuard;
	 * projects register more.
	 */
	DESIGNPATTERNSGAMEFLOW_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_FlowGuard);

	// --- Input-mode tags pushed through the shared ISeam_InputModeArbiter ---
	// The arbiter is owned by Platform; these are the mode identities the flow requests per phase as a
	// defensive default when a phase definition does not specify its own. Anchored under DP.Input.Mode.

	/** Input mode requested while a front-end / menu phase owns input (UI-only). */
	DESIGNPATTERNSGAMEFLOW_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputMode_Menu);

	/** Input mode requested during active gameplay (game-only). */
	DESIGNPATTERNSGAMEFLOW_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputMode_Game);

	/** Input mode requested while paused (game-and-UI so the pause screen takes input). */
	DESIGNPATTERNSGAMEFLOW_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputMode_Pause);

	// --- Message-bus channels (all under the core DP.Bus root) ---

	/** Broadcast after the active phase changes; payload is FFlow_PhaseChangedPayload. */
	DESIGNPATTERNSGAMEFLOW_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_PhaseChanged);

	/**
	 * Broadcast to the UI mediator to PUSH the phase's screen onto a UI layer; payload is
	 * FFlow_ScreenRequestPayload (ScreenTag + LayerTag). The HUD/UI module listens and realises it.
	 */
	DESIGNPATTERNSGAMEFLOW_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_ScreenPush);

	/** Broadcast to the UI mediator to POP the previously-pushed phase screen; payload FFlow_ScreenRequestPayload. */
	DESIGNPATTERNSGAMEFLOW_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_ScreenPop);

	/** Broadcast on loading-progress updates; payload FFlow_LoadingProgressPayload. */
	DESIGNPATTERNSGAMEFLOW_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_LoadingProgress);

	/** Broadcast when matchmaking state changes; payload FFlow_MatchmakingPayload. */
	DESIGNPATTERNSGAMEFLOW_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_MatchmakingChanged);

	/** Broadcast when level travel is about to begin; payload FFlow_TravelPayload. */
	DESIGNPATTERNSGAMEFLOW_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_TravelStarted);

	/** Broadcast on a travel/connection failure; payload FFlow_TravelPayload (bFailed=true). */
	DESIGNPATTERNSGAMEFLOW_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_TravelFailed);

	/** Broadcast as the boot sequence advances a step; payload FFlow_BootStepPayload. */
	DESIGNPATTERNSGAMEFLOW_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_BootStepChanged);

	/**
	 * Broadcast (MP only) as a hint that the game should autosave because the app was suspended while the
	 * pause controller could not engine-pause (multiplayer never hard-pauses). Payload-less. A save UI /
	 * GameMode listens. Cosmetic/advisory; the flow never itself writes a save.
	 */
	DESIGNPATTERNSGAMEFLOW_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_AutoSaveHint);

	// --- Boot-step kind tags (the data-driven boot sequence's step identities) ---
	// Each UFlow_BootStepDefinition declares one of these as its StepKind so the controller can run the
	// appropriate built-in side effect (legal screen, preload, profile load, first-run). Anchored under
	// Flow.BootStep. Games may author additional step kinds freely (the controller treats unknown kinds as
	// a pure timed/preload step).

	/** Legal / age-gate / logo step (timed display, optional screen). */
	DESIGNPATTERNSGAMEFLOW_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(BootStep_Legal);

	/** Asset preload step (front-loads the step's soft refs through the loading screen). */
	DESIGNPATTERNSGAMEFLOW_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(BootStep_Preload);

	/** Profile-load step (loads the player profile through ISeam_SaveSlotManager / the profile subsystem). */
	DESIGNPATTERNSGAMEFLOW_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(BootStep_ProfileLoad);

	/** First-run-only step (e.g. EULA / initial settings); skipped after the first launch. */
	DESIGNPATTERNSGAMEFLOW_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(BootStep_FirstRun);

	// --- Flow-guard deny reasons (DP.Flow.Guard.Reason.*) ---

	/** A guard denied a transition because no player profile / save slot is available yet. */
	DESIGNPATTERNSGAMEFLOW_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GuardReason_NoProfile);

	// --- Matchmaking phase tags (DP.Flow.Matchmaking.Phase.*) ---
	// The matchmaking controller projects the net-session seam phase onto these for UI/analytics. They are
	// matchmaking-flow phases, distinct from the top-level Flow.Phase.* FSM states.

	/** No matchmaking in progress. */
	DESIGNPATTERNSGAMEFLOW_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(MMPhase_Idle);

	/** Searching for sessions. */
	DESIGNPATTERNSGAMEFLOW_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(MMPhase_Searching);

	/** Creating (hosting) or joining a session. */
	DESIGNPATTERNSGAMEFLOW_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(MMPhase_Connecting);

	/** In a session (matchmaking succeeded). */
	DESIGNPATTERNSGAMEFLOW_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(MMPhase_Active);

	/** Matchmaking failed (a retry may be scheduled, or it is terminal). */
	DESIGNPATTERNSGAMEFLOW_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(MMPhase_Failed);
}

/**
 * Module implementation. Logs lifecycle through the umbrella LogDP category; all gameplay-flow
 * behaviour lives in the subsystems, not here.
 */
class FDesignPatternsGameFlowModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
