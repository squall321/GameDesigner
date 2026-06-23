// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Lvl_PacingBusPayloads.generated.h"

/**
 * Message-bus payload broadcast when a region's pacing crosses an escalate/relax threshold
 * (DP.Bus.Lvl.Pacing.Escalated / .Relaxed). Pure value type carried inside an FInstancedStruct by
 * UDP_MessageBusSubsystem::BroadcastPayload. Cosmetic/local only — pacing decisions are authority-side,
 * but the resulting encounter state replicates through the AI director's own actors, not this event.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSLEVELDIRECTOR_API FLvl_PacingEventPayload
{
	GENERATED_BODY()

	/** Region whose pacing changed. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Lvl|Pacing")
	FGameplayTag RegionTag;

	/** The encounter id driven for this region (the same tag the seam adapter maps to an asset). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Lvl|Pacing")
	FGameplayTag EncounterId;

	/** The normalized tension (0..1) at the moment of the crossing. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Lvl|Pacing")
	float Tension = 0.f;

	/** The normalized ProgressionInput (0..1) handed to the encounter director on this crossing. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Lvl|Pacing")
	float ProgressionInput = 0.f;

	/** True for an Escalated event, false for a Relaxed event. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Lvl|Pacing")
	bool bEscalated = false;

	FLvl_PacingEventPayload() = default;
};
