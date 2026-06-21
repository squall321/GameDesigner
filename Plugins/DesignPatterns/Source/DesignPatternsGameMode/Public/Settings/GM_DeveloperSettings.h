// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "GM_DeveloperSettings.generated.h"

class UGM_RulesetDefinition;

/**
 * Project-wide configuration for the DesignPatternsGameMode module. Appears under
 * Project Settings -> Plugins -> Design Patterns Game Mode. Editing here requires no code.
 *
 * These are the genre-neutral tunables for the match flow / scoring plumbing: how often the match
 * FSM re-evaluates ruleset conditions on the authority, the default ruleset used when a GameState
 * carries none, and the default scoreboard bucket key. The match component and score subsystem read
 * these from the CDO; when the CDO is somehow null (extreme early-load / cooked-content edge cases)
 * they fall back to the documented inline defensive defaults baked next to each consumer. There are
 * NO hardcoded magic gameplay numbers in the subsystem logic - every tunable lives here or in the
 * per-match UGM_RulesetDefinition data asset.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Design Patterns Game Mode"))
class DESIGNPATTERNSGAMEMODE_API UGM_DeveloperSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UGM_DeveloperSettings();

	//~ Begin UDeveloperSettings
	/** Group under the "Plugins" category in Project Settings. */
	virtual FName GetCategoryName() const override { return FName("Plugins"); }
	//~ End UDeveloperSettings

	// ---- Match evaluation cadence -------------------------------------------------------------

	/**
	 * How many times per second the authority re-evaluates the active ruleset's win/lose conditions
	 * while the match is in progress. This is a coordination cadence, NOT a gameplay rate; keeping it
	 * modest avoids evaluating heavy conditions every frame. Evaluation also happens immediately on
	 * explicit score/state pushes, so a low cadence does not delay event-driven transitions.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Match|Pacing", meta = (ClampMin = "0.5", ClampMax = "60.0", UIMin = "1.0", UIMax = "20.0"))
	float ConditionEvalHz = 4.f;

	/**
	 * When true, a brand-new match auto-advances out of WaitingToStart as soon as its start conditions
	 * are first satisfied. When false, the match stays in WaitingToStart until something explicitly
	 * calls StartMatch (e.g. a lobby ready-up flow). Pure flow policy.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Match|Pacing")
	bool bAutoStartWhenReady = true;

	/**
	 * Seconds the match dwells in RoundOver before automatically advancing to the next round (or to
	 * MatchOver if no rounds remain). 0 means the match component waits for an explicit advance call
	 * instead of auto-advancing. Defensive: clamped non-negative.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Match|Pacing", meta = (ClampMin = "0.0", ClampMax = "120.0", UIMin = "0.0", UIMax = "30.0"))
	float RoundOverIntermissionSeconds = 5.f;

	// ---- Default ruleset ----------------------------------------------------------------------

	/**
	 * Ruleset used by a match when the GameState's match component is not given an explicit one. A
	 * project that always assigns a ruleset per GameMode can leave this empty; the match component then
	 * runs in a documented inert mode (no auto win/lose, manual transitions only).
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Match|Ruleset", meta = (AllowedClasses = "/Script/DesignPatternsGameMode.GM_RulesetDefinition"))
	TSoftObjectPtr<UGM_RulesetDefinition> DefaultRuleset;

	// ---- Scoring ------------------------------------------------------------------------------

	/**
	 * Score bucket key used when a caller adds/sets score without specifying one. Anchor under
	 * DP.Score.*. Defaults to DP.Score.Default (set in the ctor); a project can repoint it.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Score", meta = (Categories = "DP.Score"))
	FGameplayTag DefaultScoreBucket;

	/**
	 * When true the score subsystem spawns its replicated carrier lazily on the first authoritative
	 * write; when false it spawns the carrier as soon as the world has a GameState (so clients can read
	 * a zeroed scoreboard immediately). Pure plumbing policy.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Score")
	bool bSpawnScoreCarrierEagerly = true;

	/** Convenience accessor (never null in a running game; the CDO carries the config). */
	static const UGM_DeveloperSettings* Get();
};
