// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "State/Move_MovementState.h"
#include "Move_AirStates.generated.h"

/**
 * Jump: the first airborne arc. On enter it resets the mid-air jump budget on the blackboard (from the
 * profile / settings) and launches via ACharacter::Jump. Applies the profile's air control.
 */
UCLASS()
class DESIGNPATTERNSMOVEMENT_API UMove_State_Jump : public UMove_MovementState
{
	GENERATED_BODY()

public:
	UMove_State_Jump();
	virtual void OnEnter_Implementation(UDP_StateMachineComponent* OwningComponent) override;
};

/**
 * DoubleJump: an additional mid-air jump. Entry is gated (CanEnter) on a positive air-jump budget held
 * on the blackboard; on enter it decrements the budget and applies an upward impulse. This is the
 * single owner of the air-jump budget decrement.
 */
UCLASS()
class DESIGNPATTERNSMOVEMENT_API UMove_State_DoubleJump : public UMove_MovementState
{
	GENERATED_BODY()

public:
	UMove_State_DoubleJump();
	virtual bool CanEnter_Implementation(UDP_StateMachineComponent* OwningComponent) const override;
	virtual void OnEnter_Implementation(UDP_StateMachineComponent* OwningComponent) override;
};

/**
 * Dash: a brief high-velocity directional burst. This state does TRANSLATION ONLY — it applies the dash
 * velocity and ends itself when the dash phase elapses (PhaseEndTime on the blackboard). The i-frame tag
 * lifetime and cooldown are owned by UMove_DashAction (the single owner of the i-frame tag); the state
 * never touches i-frames. Direction comes from move intent (or facing when intent is zero).
 */
UCLASS()
class DESIGNPATTERNSMOVEMENT_API UMove_State_Dash : public UMove_MovementState
{
	GENERATED_BODY()

public:
	UMove_State_Dash();
	virtual void OnEnter_Implementation(UDP_StateMachineComponent* OwningComponent) override;
	virtual void OnTick_Implementation(UDP_StateMachineComponent* OwningComponent, float DeltaSeconds) override;
	virtual void OnExit_Implementation(UDP_StateMachineComponent* OwningComponent) override;
};
