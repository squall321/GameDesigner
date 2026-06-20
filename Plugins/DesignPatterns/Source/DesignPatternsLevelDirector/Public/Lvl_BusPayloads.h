// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Lvl_BusPayloads.generated.h"

/**
 * Message-bus payload broadcast when a procedural placement pass is generated or cleared
 * (DP.Bus.Lvl.Placement.Generated / .Cleared). Pure value type — carried inside an FInstancedStruct
 * by UDP_MessageBusSubsystem::BroadcastPayload. Cosmetic/local only: nothing here crosses the wire,
 * clients learn of the pass from the replicated placed actors and this locally-republished event.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSLEVELDIRECTOR_API FLvl_PlacementEventPayload
{
	GENERATED_BODY()

	/** DataTag of the rule set that drove the pass. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Lvl|Placement")
	FGameplayTag RuleSetTag;

	/** Logical region/owner the pass belongs to. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Lvl|Placement")
	FGameplayTag RegionTag;

	/** The deterministic seed the pass used. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Lvl|Placement")
	int32 RandomSeed = 0;

	/** Number of actors placed (0 for a Cleared event). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Lvl|Placement")
	int32 PlacedCount = 0;

	FLvl_PlacementEventPayload() = default;
};

/**
 * Message-bus payload broadcast when a region's encounter is (de)activated by the encounter
 * activator (DP.Bus.Lvl.Encounter.Activated / .Deactivated). Local/cosmetic only.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSLEVELDIRECTOR_API FLvl_EncounterEventPayload
{
	GENERATED_BODY()

	/** Region the encounter belongs to. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Lvl|Encounter")
	FGameplayTag RegionTag;

	/** The activation gate key evaluated. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Lvl|Encounter")
	FGameplayTag GateKey;

	/** True for an Activated event, false for a Deactivated event. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Lvl|Encounter")
	bool bActive = false;

	FLvl_EncounterEventPayload() = default;
};
