// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"

#include "Tut_DeveloperSettings.generated.h"

/**
 * Project settings for the DesignPatternsTutorial module (Project Settings > Plugins > DesignPatterns Tutorial).
 *
 * Centralizes the few project-level choices the tutorial + hint subsystems read at startup. Every value is an
 * EditAnywhere config property (no magic constants in code); subsystems read this CDO and fall back to the
 * documented defensive defaults below when it is unavailable (e.g. very early load). Designer-facing content
 * (the tutorials/hints themselves) lives in data assets resolved through the core data registry, NOT here.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "DesignPatterns Tutorial"))
class DESIGNPATTERNSTUTORIAL_API UTut_DeveloperSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UTut_DeveloperSettings();

	//~ Begin UDeveloperSettings
	/** Groups these settings under the Plugins category in Project Settings. */
	virtual FName GetCategoryName() const override { return FName(TEXT("Plugins")); }
	//~ End UDeveloperSettings

	// --- Tutorial ---

	/**
	 * Input-mode tag the tutorial runner pushes through ISeam_InputModeArbiter for a step that gates input but
	 * leaves its own InputModeTag unset. Defaults (in the constructor) to DP.Input.Mode.Tutorial.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Tutorial", meta = (Categories = "DP.Input.Mode"))
	FGameplayTag DefaultTutorialInputModeTag;

	/**
	 * Priority the tutorial input mode is pushed at on the arbiter. Higher beats lower competing pushes (a
	 * menu, a cutscene). Tunable so projects can order their UI/game input owners relative to tutorials.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Tutorial", meta = (ClampMin = "0"))
	int32 TutorialInputModePriority = 200;

	/**
	 * When true the runner records a tutorial as completed even if it is skipped, so a skipped tutorial never
	 * replays. When false, only fully-completed tutorials are suppressed on replay. Defaults true.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Tutorial")
	bool bSkipCountsAsCompleted = true;

	/**
	 * Tutorials auto-started (by DataTag) when the tutorial subsystem initializes, in order, if not already
	 * completed. Empty means a project starts tutorials explicitly via StartTutorial. Each tag identifies a
	 * UTut_TutorialDefinition in the data registry.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Tutorial", meta = (Categories = "DP.Data.Tutorial"))
	TArray<FGameplayTag> AutoStartTutorials;

	// --- Hint ---

	/**
	 * Global minimum seconds between ANY two hints surfacing, regardless of per-hint cooldowns. Prevents a
	 * burst of competing hints from spamming the player. Tunable.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Hint", meta = (ClampMin = "0.0"))
	float GlobalHintCooldownSeconds = 5.f;

	/**
	 * How often (seconds) the hint subsystem re-evaluates hub-condition-driven hints. Bus-driven hints
	 * evaluate immediately on the event; this cadence catches purely state-driven hints. Tunable.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Hint", meta = (ClampMin = "0.05"))
	float HintEvaluationIntervalSeconds = 1.f;

	/**
	 * Default on-screen duration (seconds) for a surfaced hint when its definition leaves Duration <= 0.
	 * Passed to the HUD notification request. Tunable.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Hint", meta = (ClampMin = "0.0"))
	float DefaultHintDisplaySeconds = 6.f;

	/**
	 * Hints registered (by DataTag) with the hint subsystem on initialize. Each identifies a
	 * UTut_HintDefinition in the data registry. A project may also register hints at runtime.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Hint", meta = (Categories = "DP.Data.Hint"))
	TArray<FGameplayTag> RegisteredHints;

	// --- Diagnostics ---

	/** When true the tutorial + hint subsystems start with verbose logging enabled. */
	UPROPERTY(EditAnywhere, Config, Category = "Debug")
	bool bVerboseLogging = false;

	/** Convenience CDO accessor (never null at runtime for a UDeveloperSettings). */
	static const UTut_DeveloperSettings* Get();

	// --- Documented defensive fallbacks (used when the CDO is somehow unavailable) ---

	/** Fallback global hint cooldown when the settings CDO is null. */
	static constexpr float DefaultGlobalHintCooldownSeconds = 5.f;

	/** Fallback hint re-evaluation cadence when the settings CDO is null. */
	static constexpr float DefaultHintEvaluationIntervalSeconds = 1.f;

	/** Fallback hint display duration when the settings CDO is null. */
	static constexpr float FallbackHintDisplaySeconds = 6.f;

	/** Fallback tutorial input-mode priority when the settings CDO is null. */
	static constexpr int32 FallbackTutorialInputModePriority = 200;
};
