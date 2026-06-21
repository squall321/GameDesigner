// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "GM_MatchTypes.generated.h"

/**
 * Flat, net/save-safe payload broadcast on GameModeNativeTags::Bus_GM_MatchStateChanged whenever the
 * match-flow FSM changes state (authority and clients, after replication).
 *
 * Carries only value types (no object refs): listeners that need the carrier resolve it through the
 * score seam / service locator. The broadcasting match component passes itself as the message Instigator
 * so a listener can reach the owning GameState when it genuinely needs the live object.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSGAMEMODE_API FGM_MatchStateChangedPayload
{
	GENERATED_BODY()

	/** The previous match-state tag (invalid for the very first transition out of the initial state). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|GameMode|Match")
	FGameplayTag OldState;

	/** The new match-state tag (one of GameModeNativeTags::Match_*). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|GameMode|Match")
	FGameplayTag NewState;

	/** The 1-based current round at the moment of the change (0 before the first round starts). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|GameMode|Match")
	int32 Round = 0;
};

/**
 * Flat, net/save-safe payload broadcast on GameModeNativeTags::Bus_GM_ScoreChanged when a single
 * scoreboard bucket's value changes on the authority. Lets HUD / audio / analytics react to scoring
 * without depending on the carrier's concrete type (they read the rest of the board through the seam).
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSGAMEMODE_API FGM_ScoreChangedPayload
{
	GENERATED_BODY()

	/** The bucket key whose score changed (a team tag / category). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|GameMode|Score")
	FGameplayTag Key;

	/** The bucket's new (post-change) score. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|GameMode|Score")
	int64 NewScore = 0;

	/** The signed change applied to reach NewScore (NewScore - previous). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|GameMode|Score")
	int64 Delta = 0;
};

/**
 * Flat, net/save-safe payload broadcast on GameModeNativeTags::Bus_GM_MatchDecided when the match is
 * fully decided (entering MatchOver). The winning key is empty for a draw. Game-flow uses this to advance
 * to a results phase; analytics records the outcome.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSGAMEMODE_API FGM_MatchDecidedPayload
{
	GENERATED_BODY()

	/** The winning bucket key, or an empty tag for a draw / no-winner outcome. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|GameMode|Match")
	FGameplayTag WinningKey;

	/** The winning bucket's final score (0 when there is no winner). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|GameMode|Match")
	int64 WinningScore = 0;
};
