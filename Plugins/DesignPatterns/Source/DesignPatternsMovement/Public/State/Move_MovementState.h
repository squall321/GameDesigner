// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FSM/DPState.h"
#include "GameplayTagContainer.h"
#include "Move_MovementState.generated.h"

class UMove_MovementComponent;
class ACharacter;
class UCharacterMovementComponent;

/** Which engine movement domain a state drives the CMC into. */
UENUM(BlueprintType)
enum class EMove_MotionDomain : uint8
{
	/** Grounded (MOVE_Walking). */
	Ground,
	/** Airborne (MOVE_Falling). */
	Air,
	/** In water (MOVE_Swimming). */
	Water,
	/** Custom/flying (MOVE_Flying) for climb/wall-run. */
	Custom
};

/**
 * Optional shared motion-config block a movement state can carry. Bundles the few cross-cutting
 * flags every domain cares about so a subclass declares intent in data rather than code. All numeric
 * tuning still comes from the active UMove_LocomotionProfile — this struct holds only structural flags.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSMOVEMENT_API FMove_StateMotionConfig
{
	GENERATED_BODY()

	/** The engine domain this state drives the CMC into on enter. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement")
	EMove_MotionDomain Domain = EMove_MotionDomain::Ground;

	/** If true, the state orients the character to its movement input each tick. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement")
	bool bOrientToMovement = true;

	/** If true, the state consumes the movement controller's facing intent (strafe/aim). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement")
	bool bUseFacingIntent = false;
};

/**
 * Base class for every movement state in the locomotion FSM. A UDP_State subclass, hence STATELESS and
 * SHARED across all components using the definition — so it caches NOTHING per-instance. All per-instance
 * runtime (timers, budgets, target transforms) lives on the component's blackboard via the FName keys
 * declared below; the component caches the ACharacter / UCharacterMovementComponent (states reach them
 * through the typed accessors here, never by storing them).
 *
 * Subclasses override OnEnter/OnTick/OnExit (the UDP_State BlueprintNativeEvents) to drive the CMC and
 * read intent through ISeam_MovementController. The shared, stateless helpers below resolve the typed
 * owner objects and the active locomotion profile, and read/write the standard blackboard keys.
 */
UCLASS(Abstract, EditInlineNew, Blueprintable)
class DESIGNPATTERNSMOVEMENT_API UMove_MovementState : public UDP_State
{
	GENERATED_BODY()

public:
	/** Structural motion config (domain + orientation flags). Numeric tuning lives on the profile. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement")
	FMove_StateMotionConfig MotionConfig;

	// ---- Standard blackboard keys (FName) shared by states/guards (declared once, here) ----

	/** float: world time at which the current state's timed phase ends (dash/traversal/wall-run). */
	static const FName Key_PhaseEndTime;
	/** int: remaining mid-air jumps. */
	static const FName Key_AirJumpsRemaining;
	/** vector: server-validated traversal target location (mantle/vault pull-up). */
	static const FName Key_TraversalTargetLocation;
	/** vector: traversal start location (for interpolation). */
	static const FName Key_TraversalStartLocation;
	/** float: world time at which a traversal interpolation started. */
	static const FName Key_TraversalStartTime;
	/** float: traversal interpolation duration. */
	static const FName Key_TraversalDuration;
	/** int: wall-run side (+1 right / -1 left / 0 none). */
	static const FName Key_WallRunSide;
	/** tag: the one-shot special-move request token stamped by the intent component (authority). */
	static const FName Key_PendingSpecialMove;
	/** bool: true while a special move has been server-confirmed and is awaiting state entry. */
	static const FName Key_SpecialMoveConfirmed;

protected:
	/** Resolve the owning movement component (the FSM component is always a UMove_MovementComponent here). */
	UMove_MovementComponent* GetMovementComponent(UDP_StateMachineComponent* OwningComponent) const;

	/** Resolve the cached ACharacter from the movement component, or null. */
	ACharacter* GetCharacter(UDP_StateMachineComponent* OwningComponent) const;

	/** Resolve the cached UCharacterMovementComponent, or null. */
	UCharacterMovementComponent* GetCMC(UDP_StateMachineComponent* OwningComponent) const;

	/** Current world time in seconds (0 if no world). */
	float GetWorldTime(UDP_StateMachineComponent* OwningComponent) const;

	/**
	 * Drive the CMC max-walk/orientation flags for a ground/water domain from a profile MaxSpeed.
	 * Centralizes the "apply a speed" boilerplate so concrete ground states stay tiny.
	 */
	void ApplyGroundSpeed(UDP_StateMachineComponent* OwningComponent, float MaxSpeed) const;
};
