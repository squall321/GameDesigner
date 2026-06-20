// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "AI_BusPayloads.generated.h"

/**
 * Flat, weak-ref-free payload broadcast on DP.Bus.AI.Wave.* by the spawn director.
 *
 * Carried inside the message bus's FInstancedStruct. Deliberately holds only plain replicable/value
 * fields (tags + ints) — NO UObject pointers and NO weak refs — so it is safe to queue for deferred
 * dispatch and to flatten across the bus, exactly like the world-hub change payload.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSAI_API FAI_WaveEventPayload
{
	GENERATED_BODY()

	/** Identity of the encounter that owns the wave. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|AI|Spawn")
	FGameplayTag EncounterTag;

	/** Identity of the wave this event concerns. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|AI|Spawn")
	FGameplayTag WaveTag;

	/** Zero-based index of the wave within its encounter's wave list. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|AI|Spawn")
	int32 WaveIndex = 0;

	/** How many participants the wave intends to (or did) spawn. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|AI|Spawn")
	int32 PlannedCount = 0;

	/** How many participants were actually spawned (may be < PlannedCount if budget capped it). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|AI|Spawn")
	int32 SpawnedCount = 0;

	/** Live budget remaining at the moment of the event (after this wave's spawns, for Started/Completed). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|AI|Spawn")
	int32 RemainingBudget = 0;

	FAI_WaveEventPayload() = default;
};

/**
 * Flat payload broadcast on DP.Bus.AI.Encounter.* by the spawn director.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSAI_API FAI_EncounterEventPayload
{
	GENERATED_BODY()

	/** Identity of the encounter. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|AI|Spawn")
	FGameplayTag EncounterTag;

	/** Number of waves the encounter contains. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|AI|Spawn")
	int32 WaveCount = 0;

	/** Difficulty scalar applied to this encounter run (sampled from the difficulty curve). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|AI|Spawn")
	float DifficultyScalar = 1.f;

	FAI_EncounterEventPayload() = default;
};

/**
 * Flat payload broadcast on DP.Bus.AI.Squad.* by the squad subsystem.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSAI_API FAI_SquadEventPayload
{
	GENERATED_BODY()

	/** The affected squad. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|AI|Squad")
	FGuid SquadId;

	/** The squad's tactical/faction tag (designer-authored), if any. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|AI|Squad")
	FGameplayTag SquadTag;

	/** Member count at the moment of the event. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|AI|Squad")
	int32 MemberCount = 0;

	FAI_SquadEventPayload() = default;
};
