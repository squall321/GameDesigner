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

	/** Convenience CDO accessor (never null at runtime for a UDeveloperSettings). */
	static const UFlow_DeveloperSettings* Get();
};
