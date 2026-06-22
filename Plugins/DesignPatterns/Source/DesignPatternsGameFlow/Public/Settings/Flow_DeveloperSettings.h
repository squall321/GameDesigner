// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "Flow_DeveloperSettings.generated.h"

class UFlow_FlowStateDefinition;

/**
 * Project settings for the DesignPatternsGameFlow module
 * (Project Settings > Plugins > DesignPatterns Game Flow).
 *
 * Centralises the few project-level choices the flow subsystems read at startup: the initial phase the
 * flow FSM enters, the data registry tag-prefix to look up per-phase UFlow_FlowStateDefinition assets,
 * the default input-mode tags pushed per phase, the priority the flow pushes its input mode at, the UI
 * layer phase screens are pushed onto, and the loading-screen behaviour (movie-player auto-completion
 * and minimum on-screen time).
 *
 * Everything is an EditAnywhere config property — NO magic gameplay constants in code. The subsystems
 * read this CDO and fall back to documented defensive defaults when it is unavailable (e.g. very early
 * load). Per-phase content (level, screen, transitions) lives in data assets, not here.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "DesignPatterns Game Flow"))
class DESIGNPATTERNSGAMEFLOW_API UFlow_DeveloperSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UFlow_DeveloperSettings();

	//~ Begin UDeveloperSettings
	/** Groups these settings under the Plugins category in Project Settings. */
	virtual FName GetCategoryName() const override { return FName(TEXT("Plugins")); }
	//~ End UDeveloperSettings

	// --- Flow FSM ---

	/**
	 * The phase the flow FSM enters on first initialize (defaults to Flow.Phase.Boot in the ctor).
	 * A title screen game might repoint this to Flow.Phase.Title.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Flow", meta = (Categories = "Flow.Phase"))
	FGameplayTag InitialPhase;

	/**
	 * Per-phase definition data assets, in priority order. Each maps a phase tag to its level, screen,
	 * allowed transitions and input mode. The flow subsystem resolves the active phase's definition from
	 * this list (it also falls back to the core data registry by DataTag when a phase is absent here).
	 * Soft refs so the settings object stays lightweight; loaded on first use.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Flow",
		meta = (AllowedClasses = "/Script/DesignPatternsGameFlow.Flow_FlowStateDefinition"))
	TArray<TSoftObjectPtr<UFlow_FlowStateDefinition>> PhaseDefinitions;

	/**
	 * When true (default), transitions not explicitly listed in a phase's AllowedTransitions are still
	 * permitted (open graph). When false, only listed transitions are allowed (strict graph). A defensive
	 * default of "open" keeps an unconfigured game from deadlocking in Boot.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Flow")
	bool bAllowUndeclaredTransitions = true;

	// --- Input mode (pushed through the shared ISeam_InputModeArbiter) ---

	/** Input-mode tag pushed while a front-end/menu phase is active (defaults to DP.Input.Mode.Menu). */
	UPROPERTY(EditAnywhere, Config, Category = "Input", meta = (Categories = "DP.Input.Mode"))
	FGameplayTag MenuInputModeTag;

	/** Input-mode tag pushed during active gameplay (defaults to DP.Input.Mode.Game). */
	UPROPERTY(EditAnywhere, Config, Category = "Input", meta = (Categories = "DP.Input.Mode"))
	FGameplayTag GameInputModeTag;

	/** Input-mode tag pushed while paused (defaults to DP.Input.Mode.Pause). */
	UPROPERTY(EditAnywhere, Config, Category = "Input", meta = (Categories = "DP.Input.Mode"))
	FGameplayTag PauseInputModeTag;

	/**
	 * Priority the flow's input mode is pushed at on the shared arbiter. Higher beats lower competing
	 * pushes (a cutscene lock, photo mode). Tunable so projects can order their input owners.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Input", meta = (ClampMin = "0"))
	int32 InputModePriority = 50;

	// --- UI ---

	/**
	 * Default UI layer tag the per-phase screen is pushed onto via the UI mediator (message bus) when a
	 * phase definition does not specify its own layer. Defaults (in the ctor) to DP.UI.Layer.Menu.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "UI", meta = (Categories = "DP.UI.Layer"))
	FGameplayTag DefaultScreenLayerTag;

	// --- Loading screen ---

	/**
	 * When true (default), the loading-screen movie auto-completes (waits for the target map to finish
	 * loading rather than waiting for input). Wraps the engine FLoadingScreenAttributes::bAutoCompleteWhenLoadingCompletes.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Loading")
	bool bAutoCompleteLoadingScreen = true;

	/**
	 * When true, the loading screen blocks input while displayed. Wraps
	 * FLoadingScreenAttributes::bMoviesAreSkippable (inverted) / bAllowEngineTick semantics.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Loading")
	bool bLoadingScreenAllowsSkip = false;

	/**
	 * Minimum seconds the loading screen stays visible even if the map loads faster (avoids a one-frame
	 * flash). Wraps FLoadingScreenAttributes::MinimumLoadingScreenDisplayTime. 0 = no minimum.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Loading", meta = (ClampMin = "0.0"))
	float MinimumLoadingScreenSeconds = 0.f;

	// --- Save / continue ---

	/**
	 * When true (default), the flow subsystem records the most-recently-loaded slot (read through
	 * ISeam_SaveSlotManager) as the "continue" target so a Continue button can resume it.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Save")
	bool bTrackContinueTarget = true;

	// --- Matchmaking (driven by UFlow_MatchmakingController through ISeam_NetSession) ---

	/** Maximum automatic retries on a transient matchmaking failure before terminal NetError. */
	UPROPERTY(EditAnywhere, Config, Category = "Matchmaking", meta = (ClampMin = "0"))
	int32 MatchmakingMaxRetries = 3;

	/** Base delay (seconds) before the first matchmaking retry. */
	UPROPERTY(EditAnywhere, Config, Category = "Matchmaking", meta = (ClampMin = "0.0"))
	float RetryBaseSeconds = 2.f;

	/** Exponential backoff multiplier applied per retry attempt (delay = Base * Mult^attempt). */
	UPROPERTY(EditAnywhere, Config, Category = "Matchmaking", meta = (ClampMin = "1.0"))
	float RetryBackoffMultiplier = 2.f;

	/** Seconds between low-frequency polls of the net-session seam phase (the seam exposes no delegate). */
	UPROPERTY(EditAnywhere, Config, Category = "Matchmaking", meta = (ClampMin = "0.05"))
	float SessionPollIntervalSeconds = 0.5f;

	/**
	 * The phase the flow is FORCED into on a terminal matchmaking / travel / connection failure. Defaults
	 * (in the ctor) to Flow.Phase.NetError. Set to Flow.Phase.MainMenu for a game with no NetError screen.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Matchmaking", meta = (Categories = "Flow.Phase"))
	FGameplayTag NetErrorPhase;

	// --- Travel / carry-over ---

	/** The save slot the travel coordinator round-trips carry-over data through across a level travel. */
	UPROPERTY(EditAnywhere, Config, Category = "Travel")
	FString CarryOverSlotName = TEXT("_dp_carryover");

	/**
	 * When true, the carry-over slot is also mirrored into the player profile so a mid-session quit during
	 * travel can recover it. When false the carry-over slot is treated as transient (deleted after restore).
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Travel")
	bool bWriteCarryOverToProfile = true;

	// --- Loading (sublevel streaming aggregation) ---

	/**
	 * Weight of the asset-preload fraction vs the sublevel-streaming fraction when the loading coordinator
	 * combines them into one bar, in [0,1] (0 = bar is pure streaming, 1 = bar is pure preload). Default
	 * 0.5 weights them equally. Tunable so a content-heavy phase can bias the bar toward streaming.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Loading", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PreloadVsStreamWeight = 0.5f;

	/** Seconds between aggregate-progress polls while sublevels stream in (timer-driven, no ticking). */
	UPROPERTY(EditAnywhere, Config, Category = "Loading", meta = (ClampMin = "0.02"))
	float StreamingPollIntervalSeconds = 0.1f;

	// --- Pause / focus loss (driven via ISeam_AppLifecycle) ---

	/** When true, the flow auto-pauses (standalone) on OS focus loss / app suspend. */
	UPROPERTY(EditAnywhere, Config, Category = "Pause")
	bool bAutoPauseOnFocusLoss = true;

	/**
	 * When false (default), the flow never engine-pauses in multiplayer on focus loss (you can't pause a
	 * shared world) — it pushes the pause overlay and emits an autosave hint instead.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Pause")
	bool bAllowPauseInMultiplayer = false;

	// --- Boot sequence ---

	/** The data-driven boot sequence the boot controller runs while in Flow.Phase.Boot (soft ref). */
	UPROPERTY(EditAnywhere, Config, Category = "Boot",
		meta = (AllowedClasses = "/Script/DesignPatternsGameFlow.Flow_BootSequenceDefinition"))
	TSoftObjectPtr<class UFlow_BootSequenceDefinition> BootSequence;

	/** When true, the boot sequence is skipped in PIE (jump straight to the initial phase). */
	UPROPERTY(EditAnywhere, Config, Category = "Boot")
	bool bSkipBootInPIE = true;

	/**
	 * Config flag the boot controller reads + clears to detect a first launch (drives bFirstRunOnly steps).
	 * Stored in config so it persists across sessions; the controller sets it true after the first boot.
	 */
	UPROPERTY(Config)
	bool bHasCompletedFirstRun = false;

	// --- Flow guards / history ---

	/** Maximum depth of the flow back-stack (for GoBack). Clamped >=1. */
	UPROPERTY(EditAnywhere, Config, Category = "Flow", meta = (ClampMin = "1"))
	int32 FlowHistoryDepth = 16;

	/**
	 * When true (default), validated (non-forced) transitions are vetoed by registered ISeam_FlowGuard
	 * providers. When false the guard step is skipped entirely (ForceTransition always bypasses guards
	 * regardless). A kill-switch for projects that don't want guards.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Flow")
	bool bEnableTransitionGuards = true;

	/** Convenience CDO accessor (never null at runtime for a UDeveloperSettings). */
	static const UFlow_DeveloperSettings* Get();
};
