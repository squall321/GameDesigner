// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "WorldHub_FactionTypes.generated.h"

/** How standings between two factions are kept symmetric (or not) when one side is written. */
UENUM(BlueprintType)
enum class EWorldHub_FactionSymmetry : uint8
{
	/** A->B and B->A are independent (one-directional standings). */
	Asymmetric,

	/** Writing A->B also mirrors B->A to the same value (mutual standings). */
	Symmetric
};

/** One discrete standing tier with its inclusive lower threshold, for tier classification. */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSWORLD_API FWorldHub_FactionTier
{
	GENERATED_BODY()

	/** The tier tag (e.g. DP.WorldHub.Faction.Tier.Hostile / Neutral / Allied). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|WorldHub|Faction")
	FGameplayTag TierTag;

	/** Inclusive lower bound: a standing >= this (and below the next tier's bound) maps to this tier. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|WorldHub|Faction")
	float MinStanding = 0.0f;

	FWorldHub_FactionTier() = default;
};

/** One authored initial standing pair (A toward B). */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSWORLD_API FWorldHub_FactionStandingSeed
{
	GENERATED_BODY()

	/** The faction whose attitude is being seeded. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|WorldHub|Faction")
	FGameplayTag FactionA;

	/** The faction A's attitude is toward. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|WorldHub|Faction")
	FGameplayTag FactionB;

	/** The initial standing value of A toward B (clamped to the matrix bounds on apply). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|WorldHub|Faction")
	float Standing = 0.0f;

	FWorldHub_FactionStandingSeed() = default;
};

/**
 * Flat, weak-ref-free payload broadcast on the bus when a faction standing changes. Carried inside an
 * FInstancedStruct by the bus; holds no UObject references.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSWORLD_API FWorldHub_StandingChangedPayload
{
	GENERATED_BODY()

	/** The faction whose attitude changed. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|WorldHub|Faction")
	FGameplayTag FactionA;

	/** The faction the attitude is toward. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|WorldHub|Faction")
	FGameplayTag FactionB;

	/** The new standing value. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|WorldHub|Faction")
	float NewStanding = 0.0f;

	FWorldHub_StandingChangedPayload() = default;
	FWorldHub_StandingChangedPayload(const FGameplayTag& InA, const FGameplayTag& InB, float InStanding)
		: FactionA(InA), FactionB(InB), NewStanding(InStanding) {}
};

/**
 * Broadcast (server and clients) when a faction standing changes.
 * @param FactionA    The faction whose attitude changed.
 * @param FactionB    The faction the attitude is toward.
 * @param NewStanding The new standing value.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FWorldHub_OnStandingChanged, FGameplayTag, FactionA, FGameplayTag, FactionB, float, NewStanding);
