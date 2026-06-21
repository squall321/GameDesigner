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
