// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "GM_RespawnTypes.generated.h"

/** Why a respawn could not be performed. Returned by the respawn component so callers can react/log. */
UENUM(BlueprintType)
enum class EGM_RespawnResult : uint8
{
	/** Respawn was queued (the delay timer started) or performed immediately. */
	Succeeded			UMETA(DisplayName = "Succeeded"),

	/** Caller is not the authority (client-side request was rejected). */
	NoAuthority			UMETA(DisplayName = "No Authority"),

	/** No eligible spawn point was found (no provider, or none matched the team filter). */
	NoSpawnPoint		UMETA(DisplayName = "No Spawn Point"),

	/** A respawn is already pending for this actor. */
	AlreadyPending		UMETA(DisplayName = "Already Pending"),

	/** The per-actor auto-respawn budget (MaxAutoRespawns) is exhausted. */
	BudgetExhausted		UMETA(DisplayName = "Budget Exhausted"),

	/** The owning actor / world was invalid. */
	InvalidContext		UMETA(DisplayName = "Invalid Context")
};

/**
 * Flat payload broadcast on GMTags::Bus_Respawned after an actor is repositioned. No object refs (the
 * respawned actor travels as the bus Instigator), so it is net/save-safe for any listener.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSGAMEMODE_API FGM_RespawnedPayload
{
	GENERATED_BODY()

	/** The team the actor was on when it respawned (drives the spawn-point filter; empty = none). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|GameMode|Respawn")
	FGameplayTag TeamTag;

	/** World-space transform the actor was respawned at. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|GameMode|Respawn")
	FTransform SpawnTransform = FTransform::Identity;

	/** 1-based index of this respawn for the actor (1 = first respawn). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|GameMode|Respawn")
	int32 RespawnCount = 0;
};
