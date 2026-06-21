// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "State/Move_MovementState.h"
#include "Move_GroundStates.generated.h"

/**
 * Walk: the default grounded state. Drives the CMC to the profile's Walk speed and orients to movement.
 * Transitions out are authored on the state (to Run/Sprint/Crouch/Jump/Swim) and gated by guards that
 * read intent through the movement component.
 */
UCLASS()
class DESIGNPATTERNSMOVEMENT_API UMove_State_Walk : public UMove_MovementState
{
	GENERATED_BODY()

public:
	UMove_State_Walk();
	virtual void OnEnter_Implementation(UDP_StateMachineComponent* OwningComponent) override;
	virtual void OnTick_Implementation(UDP_StateMachineComponent* OwningComponent, float DeltaSeconds) override;
};

/** Run: grounded locomotion at the profile's Run speed (full analog input, no sprint modifier). */
UCLASS()
class DESIGNPATTERNSMOVEMENT_API UMove_State_Run : public UMove_MovementState
{
	GENERATED_BODY()

public:
	UMove_State_Run();
	virtual void OnEnter_Implementation(UDP_StateMachineComponent* OwningComponent) override;
};

/**
 * Sprint: grounded locomotion at the profile's Sprint speed. While active and on AUTHORITY, drains the
 * stamina component each tick (scaled by the sim clock inside the component). Entry is gated by a guard
 * that requires stamina and non-exhaustion; the state also self-exits when stamina is depleted.
 */
UCLASS()
class DESIGNPATTERNSMOVEMENT_API UMove_State_Sprint : public UMove_MovementState
{
	GENERATED_BODY()

public:
	UMove_State_Sprint();
	virtual void OnEnter_Implementation(UDP_StateMachineComponent* OwningComponent) override;
	virtual void OnTick_Implementation(UDP_StateMachineComponent* OwningComponent, float DeltaSeconds) override;
	virtual bool CanEnter_Implementation(UDP_StateMachineComponent* OwningComponent) const override;
};

/** Crouch: reduced speed and capsule height (uses ACharacter::Crouch/UnCrouch). */
UCLASS()
class DESIGNPATTERNSMOVEMENT_API UMove_State_Crouch : public UMove_MovementState
{
	GENERATED_BODY()

public:
	UMove_State_Crouch();
	virtual void OnEnter_Implementation(UDP_StateMachineComponent* OwningComponent) override;
	virtual void OnExit_Implementation(UDP_StateMachineComponent* OwningComponent) override;
};

/**
 * Slide: a momentum slide entered from a sprinting crouch. Slope-aware: on a floor steeper than the
 * profile's threshold it adds downhill acceleration scaled by steepness; on flat ground it decays via
 * the CMC braking. Self-exits when speed falls below the profile's minimum slide speed.
 */
UCLASS()
class DESIGNPATTERNSMOVEMENT_API UMove_State_Slide : public UMove_MovementState
{
	GENERATED_BODY()

public:
	UMove_State_Slide();
	virtual void OnEnter_Implementation(UDP_StateMachineComponent* OwningComponent) override;
	virtual void OnTick_Implementation(UDP_StateMachineComponent* OwningComponent, float DeltaSeconds) override;
	virtual void OnExit_Implementation(UDP_StateMachineComponent* OwningComponent) override;
};
