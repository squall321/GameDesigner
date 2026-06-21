// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "Score/Seam_ScoreSource.h"

// FInstancedStruct (read by the _HubFlag condition from the world-hub seam). Engine moved the header from
// the StructUtils module into CoreUObject in UE 5.5; this version-gated include must precede generated.h.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "GM_Condition.generated.h"

/**
 * Abstract, instanced base for a single match win/lose CONDITION.
 *
 * Conditions are composable predicate objects authored INLINE inside a UGM_RulesetDefinition (so a
 * designer builds "win if score >= 50 OR time elapsed" as a list of these). Each subclass implements a
 * BlueprintNativeEvent Evaluate that returns true when the condition currently holds. The match-state
 * component evaluates them on the AUTHORITY only, against live world state read THROUGH SEAMS (score,
 * team, world-hub) so a condition never hard-depends on a concrete gameplay system.
 *
 * EditInlineNew + Abstract: a designer picks a concrete subclass per slot in the ruleset's condition
 * arrays; this base is never instantiated directly. Conditions hold no replicated state and are pure
 * read predicates - they must not mutate the world.
 */
UCLASS(Abstract, EditInlineNew, BlueprintType, DefaultToInstanced, CollapseCategories)
class DESIGNPATTERNSGAMEMODE_API UGM_Condition : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Evaluate this condition against the current world state.
	 *
	 * @param WorldContext  Any object that can resolve a UWorld (the match component passes itself or its
	 *                      owning GameState). Implementations resolve subsystems/seams from it.
	 * @return true if the condition currently holds. Implementations must be side-effect free.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|GameMode|Ruleset")
	bool Evaluate(UObject* WorldContext) const;
	virtual bool Evaluate_Implementation(UObject* WorldContext) const;

	/** One-line description for debug/tooling UIs. Overridden per concrete condition. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|GameMode|Ruleset")
	FText GetConditionDescription() const;
	virtual FText GetConditionDescription_Implementation() const;

protected:
	/** Helper: resolve the world's score read-seam (ISeam_ScoreSource) via the service locator, or null. */
	TScriptInterface<ISeam_ScoreSource> ResolveScoreSource(const UObject* WorldContext) const;
};

/**
 * True when the score for a given bucket key reaches a threshold.
 *
 * Reads scores through the ISeam_ScoreSource seam (the replicated score carrier), so it works on the
 * authority where conditions are evaluated. Use for "first team to N points wins" rulesets.
 */
UCLASS(meta = (DisplayName = "Score At Least"))
class DESIGNPATTERNSGAMEMODE_API UGM_Condition_ScoreAtLeast : public UGM_Condition
{
	GENERATED_BODY()

public:
	virtual bool Evaluate_Implementation(UObject* WorldContext) const override;
	virtual FText GetConditionDescription_Implementation() const override;

	/** Score bucket to test (a team tag or category). Empty matches the ruleset/settings default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition", meta = (Categories = "DP.Score"))
	FGameplayTag ScoreKey;

	/** The score the bucket must reach (>=) for this condition to hold. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition", meta = (ClampMin = "0"))
	int64 Threshold = 0;

	/**
	 * When true the condition tests the HIGHEST score across ALL buckets instead of a single key (useful
	 * for "any team reaching N ends the match"). ScoreKey is ignored in that mode.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition")
	bool bAnyBucket = false;
};

/**
 * True once a given amount of match time has elapsed.
 *
 * Time is measured from when the condition is first evaluated in-progress (the match component seeds the
 * reference time on entering InProgress and passes elapsed seconds via the world's timer). Implemented by
 * reading the owning world's TimeSeconds against a stored start captured by the match component, so the
 * condition itself stays stateless - it compares the ruleset TimeLimit against the component's elapsed.
 */
UCLASS(meta = (DisplayName = "Time Elapsed"))
class DESIGNPATTERNSGAMEMODE_API UGM_Condition_TimeElapsed : public UGM_Condition
{
	GENERATED_BODY()

public:
	virtual bool Evaluate_Implementation(UObject* WorldContext) const override;
	virtual FText GetConditionDescription_Implementation() const override;

	/**
	 * Seconds of in-progress match time after which this condition holds. 0 means "use the ruleset's
	 * TimeLimitSeconds" (so a designer can set the limit in one place). Defensive: clamped non-negative.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition", meta = (ClampMin = "0.0"))
	float Seconds = 0.f;
};

/**
 * True when only one team (or zero) still has any live presence - the classic "last team standing".
 *
 * Counts distinct team tags among relevant actors via the ISeam_TeamAffinity seam resolved off the world.
 * Because team membership is policy-driven behind the seam, this works for FFA (each actor its own team)
 * and team modes alike. When the seam is absent the condition degrades to false (documented inert).
 */
UCLASS(meta = (DisplayName = "Last Team Standing"))
class DESIGNPATTERNSGAMEMODE_API UGM_Condition_LastTeamStanding : public UGM_Condition
{
	GENERATED_BODY()

public:
	virtual bool Evaluate_Implementation(UObject* WorldContext) const override;
	virtual FText GetConditionDescription_Implementation() const override;

	/**
	 * Optional actor-class filter: only actors of this class (and subclasses) count toward team presence.
	 * Empty counts every team-tagged actor the seam recognises. Keeps the test cheap and meaningful
	 * (e.g. only pawns count, not pickups).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition")
	TSoftClassPtr<AActor> CountableClass;

	/** True (default) holds when <= 1 team remains; false requires exactly 0 teams remaining. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition")
	bool bAllowSingleSurvivor = true;
};

/**
 * True when a world-hub flag matches an expected boolean value.
 *
 * Reads the World module's IWorldHub_Queryable seam (resolved from the service locator) so a ruleset can
 * gate on designer-authored world flags ("objective captured", "reactor destroyed") without this module
 * depending on the World module's concrete types. When the hub is absent the condition degrades to the
 * configured "missing" result (documented inert default).
 */
UCLASS(meta = (DisplayName = "Hub Flag"))
class DESIGNPATTERNSGAMEMODE_API UGM_Condition_HubFlag : public UGM_Condition
{
	GENERATED_BODY()

public:
	virtual bool Evaluate_Implementation(UObject* WorldContext) const override;
	virtual FText GetConditionDescription_Implementation() const override;

	/** The world-hub flag key to read. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition")
	FGameplayTag FlagKey;

	/** The boolean value the flag must hold for this condition to be true. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition")
	bool bExpectedValue = true;

	/**
	 * Result returned when the hub seam is unavailable or has no value for the key (a value default does
	 * not count as "has value"). Lets a designer choose fail-open vs fail-closed for an absent hub.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Condition")
	bool bResultIfMissing = false;

private:
	/**
	 * Interpret a hub value (an FInstancedStruct) as a boolean by reading its first bool field. Keeps this
	 * condition decoupled from the World module's concrete bool-flag payload type; a present value with no
	 * bool field reads as true (a set-but-non-bool flag). Static, side-effect free.
	 */
	static bool ReadBoolFromInstancedStruct(const FInstancedStruct& Value);
};
