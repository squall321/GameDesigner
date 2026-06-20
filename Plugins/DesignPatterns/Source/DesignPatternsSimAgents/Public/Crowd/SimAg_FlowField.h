// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "SimAg_FlowField.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USimAg_FlowField : public UInterface
{
	GENERATED_BODY()
};

/**
 * Flow-field seam: "which way should an agent at this point head to reach a goal, and what is the local
 * crowd-separation push here?".
 *
 * Crowd steering reads guidance through this seam so the actual field source is swappable — a real
 * precomputed flow-field generator, a nav-mesh path direction, or the module's built-in nav+separation
 * fallback all implement the same contract. The steering component composes SampleFlowDirection (the
 * "where to go" vector) with SampleSeparation (the local "don't crowd" vector) into a desired velocity.
 */
class DESIGNPATTERNSSIMAGENTS_API ISimAg_FlowField
{
	GENERATED_BODY()

public:
	/**
	 * Unit-ish world direction an agent at WorldLocation should travel to progress toward Goal. Returns
	 * a zero vector when no guidance is available (the agent then steers straight at the goal).
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|SimAgents|Crowd")
	FVector SampleFlowDirection(const FVector& WorldLocation, const FVector& Goal) const;

	/**
	 * Local crowd-separation push at WorldLocation: a world vector pointing away from nearby agents,
	 * scaled by crowding. QueryRadius bounds the neighbour search. Returns zero when uncrowded.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|SimAgents|Crowd")
	FVector SampleSeparation(const FVector& WorldLocation, float QueryRadius) const;
};
