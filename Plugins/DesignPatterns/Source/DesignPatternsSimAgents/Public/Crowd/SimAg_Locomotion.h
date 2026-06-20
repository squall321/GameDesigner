// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "SimAg_Locomotion.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USimAg_Locomotion : public UInterface
{
	GENERATED_BODY()
};

/**
 * Locomotion seam: how steering output is APPLIED to whatever moves the agent.
 *
 * This decouples the crowd steering math from the movement substrate. A CharacterMovement pawn, a
 * custom mover, an AIController path-follower, or a kinematic proxy can each implement this interface;
 * the steering component pushes its result through the seam and never hard-assumes AAIController or any
 * concrete movement component. Implementers decide whether RequestMoveTo issues a path request or just
 * stores a goal, and how SetMovementInput maps a desired velocity onto their movement model.
 */
class DESIGNPATTERNSSIMAGENTS_API ISimAg_Locomotion
{
	GENERATED_BODY()

public:
	/**
	 * Request travel toward a world-space goal. The implementer may path-find (AIController) or simply
	 * record the goal for its own movement loop. Called when the brain picks a new target.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|SimAgents|Crowd")
	void RequestMoveTo(const FVector& WorldGoal);

	/**
	 * Apply a per-frame desired movement vector (world space; magnitude in [0,1] of max speed is the
	 * convention). This is the steering/avoidance output. Implementers blend it into their movement
	 * model (e.g. AddMovementInput on a pawn). Called every steering tick while moving.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|SimAgents|Crowd")
	void SetMovementInput(const FVector& WorldDesiredVelocity);
};
