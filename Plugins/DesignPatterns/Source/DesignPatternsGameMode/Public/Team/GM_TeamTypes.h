// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "GM_TeamTypes.generated.h"

/**
 * How the team subsystem interprets the relationship between two team tags. Pure data so that
 * free-for-all, classic team, and ally-faction setups all work without code changes. Chosen per match
 * via the GameMode settings (a UPROPERTY, never a magic constant).
 */
UENUM(BlueprintType)
enum class EGM_TeamPolicy : uint8
{
	/** Everyone is hostile to everyone else; an actor is friendly only with itself. */
	FreeForAll		UMETA(DisplayName = "Free-For-All"),

	/** Actors are friendly iff they share the exact same team tag. The classic team-deathmatch rule. */
	TeamMatch		UMETA(DisplayName = "Team Match"),

	/**
	 * Actors are friendly iff their team tags are related in the gameplay-tag hierarchy (one is a parent
	 * of, or equal to, the other) OR an explicit alliance row pairs their teams. Models faction trees
	 * (DP.Team.Alliance.Red is friendly with DP.Team.Alliance) plus designer-authored alliances.
	 */
	AllyFaction		UMETA(DisplayName = "Ally / Faction")
};

/**
 * How friendly fire is resolved for combat that reads this seam. The team subsystem only PUBLISHES this
 * value (combat enforces it via AreFriendly); kept here so policy lives in one data-driven place.
 */
UENUM(BlueprintType)
enum class EGM_FriendlyFirePolicy : uint8
{
	/** Friendly actors cannot damage each other. AreFriendly()==true means "do not apply damage". */
	Disabled		UMETA(DisplayName = "Disabled (no friendly fire)"),

	/** Friendly actors can damage each other normally. Combat ignores team for damage gating. */
	Enabled			UMETA(DisplayName = "Enabled (full friendly fire)"),

	/** Friendly fire applies but at a reduced scalar (combat reads GetFriendlyFireScalar()). */
	Reduced			UMETA(DisplayName = "Reduced")
};

/**
 * One designer-authored alliance row pairing two team tags as friendly under AllyFaction policy. Both
 * directions are implied (alliances are symmetric). Authored on the GameMode settings, never hardcoded.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSGAMEMODE_API FGM_TeamAllianceRow
{
	GENERATED_BODY()

	/** First team in the alliance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|GameMode|Team")
	FGameplayTag TeamA;

	/** Second team in the alliance (friendly with TeamA). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|GameMode|Team")
	FGameplayTag TeamB;
};

/**
 * Flat, net/save-safe payload broadcast on GMTags::Bus_TeamChanged when an actor's team changes. Holds
 * no object refs (the actor is projected to its identity only as needed by the listener via Instigator).
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSGAMEMODE_API FGM_TeamChangedPayload
{
	GENERATED_BODY()

	/** The team the actor previously belonged to (empty if it had none). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|GameMode|Team")
	FGameplayTag PreviousTeam;

	/** The team the actor now belongs to (empty if it was removed from all teams). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|GameMode|Team")
	FGameplayTag NewTeam;
};
