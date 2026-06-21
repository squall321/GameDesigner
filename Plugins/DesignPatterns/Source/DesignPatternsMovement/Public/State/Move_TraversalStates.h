// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "State/Move_MovementState.h"
#include "Move_TraversalStates.generated.h"

/**
 * Base for the interpolated traversals (Mantle/Vault). Both read a server-validated target transform
 * from the blackboard (written by the intent component on authority, or locally for cosmetic prediction)
 * and interpolate the character to it over the profile's traversal duration, then hand back to a ground
 * state. The montage (if any) is a cosmetic LOCAL overlay only — there is NO authoritative root motion
 * and NO MotionWarping dependency; the authoritative motion is this interpolation, which both server and
 * client run from the same replicated target so they converge.
 */
UCLASS(Abstract)
class DESIGNPATTERNSMOVEMENT_API UMove_State_TraversalBase : public UMove_MovementState
{
	GENERATED_BODY()

public:
	virtual void OnEnter_Implementation(UDP_StateMachineComponent* OwningComponent) override;
	virtual void OnTick_Implementation(UDP_StateMachineComponent* OwningComponent, float DeltaSeconds) override;
	virtual void OnExit_Implementation(UDP_StateMachineComponent* OwningComponent) override;

protected:
	/** @return true if the interpolation has reached (or passed) its duration. */
	bool IsTraversalComplete(UDP_StateMachineComponent* OwningComponent) const;
};

/** Mantle: pull up onto a tall ledge. Uses the traversal target written from a FindLedge (non-vault) hit. */
UCLASS()
class DESIGNPATTERNSMOVEMENT_API UMove_State_Mantle : public UMove_State_TraversalBase
{
	GENERATED_BODY()

public:
	UMove_State_Mantle();
};

/** Vault: hop over a low obstacle. Uses the traversal target written from a FindLedge (vault) hit. */
UCLASS()
class DESIGNPATTERNSMOVEMENT_API UMove_State_Vault : public UMove_State_TraversalBase
{
	GENERATED_BODY()

public:
	UMove_State_Vault();
};

/**
 * WallRun: lateral run along a near-vertical wall. On enter it puts the CMC into flying mode with a
 * reduced gravity feel and records the wall side + a duration cap on the blackboard. Each tick it
 * re-confirms a wall on the recorded side (ending if the wall is lost) and biases velocity along it.
 */
UCLASS()
class DESIGNPATTERNSMOVEMENT_API UMove_State_WallRun : public UMove_MovementState
{
	GENERATED_BODY()

public:
	UMove_State_WallRun();
	virtual void OnEnter_Implementation(UDP_StateMachineComponent* OwningComponent) override;
	virtual void OnTick_Implementation(UDP_StateMachineComponent* OwningComponent, float DeltaSeconds) override;
	virtual void OnExit_Implementation(UDP_StateMachineComponent* OwningComponent) override;
};

/**
 * Climb: free climb on a climbable surface. On enter it switches the CMC to flying (gravity off); each
 * tick it moves along the surface plane from move intent and re-confirms a wall ahead (ending if lost).
 */
UCLASS()
class DESIGNPATTERNSMOVEMENT_API UMove_State_Climb : public UMove_MovementState
{
	GENERATED_BODY()

public:
	UMove_State_Climb();
	virtual void OnEnter_Implementation(UDP_StateMachineComponent* OwningComponent) override;
	virtual void OnTick_Implementation(UDP_StateMachineComponent* OwningComponent, float DeltaSeconds) override;
	virtual void OnExit_Implementation(UDP_StateMachineComponent* OwningComponent) override;
};
