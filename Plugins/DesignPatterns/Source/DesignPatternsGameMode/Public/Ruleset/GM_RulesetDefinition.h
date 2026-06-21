// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "GM_RulesetDefinition.generated.h"

class UGM_Condition;

/**
 * One team's configuration in a ruleset: a stable team tag plus a display label and a starting score.
 * Pure data; the score subsystem seeds a scoreboard bucket per entry at match start.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSGAMEMODE_API FGM_TeamConfig
{
	GENERATED_BODY()

	/** Stable team identity (e.g. DP.Team.Red). The scoreboard bucket key for this team. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Team", meta = (Categories = "DP.Team"))
	FGameplayTag TeamTag;

	/** Human-readable label shown on scoreboards / results screens. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Team")
	FText DisplayName;

	/** Score this team's bucket is seeded with at match start (usually 0). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Team")
	int64 StartingScore = 0;
};

/**
 * Respawn policy for a ruleset. All-data; the GameMode reads these to decide whether/where to respawn,
 * resolving the actual location through the LevelDirector spawn-region seam (this module never depends on
 * concrete respawn actors).
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSGAMEMODE_API FGM_RespawnRules
{
	GENERATED_BODY()

	/** When false, eliminated participants do not respawn (e.g. last-team-standing modes). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Respawn")
	bool bAllowRespawn = true;

	/** Delay (seconds) between elimination and respawn. Defensive: clamped non-negative. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Respawn", meta = (ClampMin = "0.0", EditCondition = "bAllowRespawn"))
	float RespawnDelaySeconds = 3.f;

	/**
	 * Spawn-region tag handed to the LevelDirector ILvl_SpawnRegionProvider seam when respawning. Empty
	 * lets the GameMode pick a default region. Lets a designer route teams to distinct spawn regions.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Respawn", meta = (EditCondition = "bAllowRespawn"))
	FGameplayTag SpawnRegionTag;

	/** Maximum lives per participant; 0 = unlimited. Reached-zero participants stop respawning. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Respawn", meta = (ClampMin = "0", EditCondition = "bAllowRespawn"))
	int32 MaxLives = 0;
};

/**
 * Data-only definition of a match's RULES. A designer authors one of these per game mode and assigns it to
 * the match-state component (or sets it as the project default in UGM_DeveloperSettings).
 *
 * Everything here is DATA - there is no behaviour. The match component reads the win/lose CONDITION lists
 * (instanced UGM_Condition objects) and the round/time/team/respawn config; the score subsystem reads the
 * team config to seed buckets. No magic gameplay numbers live in code: round count, time limit, per-team
 * starting scores and respawn timing all come from this asset.
 *
 * Win vs lose semantics: when ANY win condition holds the match ends with the current leader as winner;
 * when ANY lose condition holds the match ends as a loss/draw (no winner). Conditions are read THROUGH
 * SEAMS so the ruleset never couples to concrete gameplay systems.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSGAMEMODE_API UGM_RulesetDefinition : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	UGM_RulesetDefinition();

	// ---- Win / lose conditions ------------------------------------------------------------------

	/**
	 * Conditions that, when ANY is satisfied, end the match as a WIN (the current leader is the winner).
	 * Authored inline as instanced UGM_Condition subobjects. Evaluated on the authority only.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, Category = "Conditions")
	TArray<TObjectPtr<UGM_Condition>> WinConditions;

	/**
	 * Conditions that, when ANY is satisfied, end the match WITHOUT a winner (a loss/draw - e.g. time ran
	 * out with no leader, or an objective was lost). Evaluated on the authority only.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, Category = "Conditions")
	TArray<TObjectPtr<UGM_Condition>> LoseConditions;

	/**
	 * Conditions gating the WaitingToStart -> InProgress auto-start (ALL must hold). Empty means "always
	 * ready" so the match can start immediately. Only consulted when settings bAutoStartWhenReady is true.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, Category = "Conditions")
	TArray<TObjectPtr<UGM_Condition>> StartConditions;

	// ---- Round / time structure -----------------------------------------------------------------

	/**
	 * Number of rounds in the match. 1 is a single-round match. When the last round ends the match goes to
	 * MatchOver. Clamped >= 1 so a match always has at least one round.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Structure", meta = (ClampMin = "1", UIMin = "1", UIMax = "16"))
	int32 RoundCount = 1;

	/**
	 * Per-round time limit in seconds; 0 = unlimited (round ends only via conditions). Read by the
	 * TimeElapsed condition (when its own Seconds is 0) and exposed to UI as a countdown.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Structure", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "1800.0"))
	float TimeLimitSeconds = 0.f;

	// ---- Teams & scoring ------------------------------------------------------------------------

	/**
	 * Teams participating in the match. The score subsystem seeds one scoreboard bucket per entry at match
	 * start. Empty is valid (a non-team mode that scores into ad-hoc buckets).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Teams")
	TArray<FGM_TeamConfig> Teams;

	/**
	 * When true the match is decided by HIGHEST score among the team buckets at the moment a win condition
	 * fires; when false the win condition's own key (if any) names the winner. Pure tie-break policy.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Teams")
	bool bHighestScoreWins = true;

	// ---- Respawn --------------------------------------------------------------------------------

	/** Respawn policy for participants in this ruleset. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Respawn")
	FGM_RespawnRules RespawnRules;

	// ---- Helpers --------------------------------------------------------------------------------

	/**
	 * Evaluate the WIN condition list against WorldContext. @return true if ANY win condition holds. Null
	 * entries are skipped. Authority callers only (conditions are authority-evaluated).
	 */
	bool AnyWinConditionMet(UObject* WorldContext) const;

	/** As AnyWinConditionMet but for the LOSE list. */
	bool AnyLoseConditionMet(UObject* WorldContext) const;

	/** As AnyWinConditionMet but for the START list, returning true only if ALL (or none authored) hold. */
	bool AllStartConditionsMet(UObject* WorldContext) const;

#if WITH_EDITOR
	//~ Begin UObject
	/** Validates RoundCount >= 1 and warns on duplicate team tags / null condition entries. */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
	//~ End UObject
#endif
};
