// Copyright DesignPatterns plugin. All Rights Reserved.

#include "State/Move_MovementState.h"
#include "Component/Move_MovementComponent.h"
#include "Data/Move_LocomotionProfile.h"

#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"

// ---- Standard blackboard key definitions ----
const FName UMove_MovementState::Key_PhaseEndTime            = FName(TEXT("Move.PhaseEndTime"));
const FName UMove_MovementState::Key_AirJumpsRemaining       = FName(TEXT("Move.AirJumpsRemaining"));
const FName UMove_MovementState::Key_TraversalTargetLocation = FName(TEXT("Move.TraversalTargetLocation"));
const FName UMove_MovementState::Key_TraversalStartLocation  = FName(TEXT("Move.TraversalStartLocation"));
const FName UMove_MovementState::Key_TraversalStartTime      = FName(TEXT("Move.TraversalStartTime"));
const FName UMove_MovementState::Key_TraversalDuration       = FName(TEXT("Move.TraversalDuration"));
const FName UMove_MovementState::Key_WallRunSide             = FName(TEXT("Move.WallRunSide"));
const FName UMove_MovementState::Key_PendingSpecialMove      = FName(TEXT("Move.PendingSpecialMove"));
const FName UMove_MovementState::Key_SpecialMoveConfirmed    = FName(TEXT("Move.SpecialMoveConfirmed"));

UMove_MovementComponent* UMove_MovementState::GetMovementComponent(UDP_StateMachineComponent* OwningComponent) const
{
	return Cast<UMove_MovementComponent>(OwningComponent);
}

ACharacter* UMove_MovementState::GetCharacter(UDP_StateMachineComponent* OwningComponent) const
{
	if (UMove_MovementComponent* Move = GetMovementComponent(OwningComponent))
	{
		return Move->GetCharacter();
	}
	return nullptr;
}

UCharacterMovementComponent* UMove_MovementState::GetCMC(UDP_StateMachineComponent* OwningComponent) const
{
	if (UMove_MovementComponent* Move = GetMovementComponent(OwningComponent))
	{
		return Move->GetCharacterMovement();
	}
	return nullptr;
}

float UMove_MovementState::GetWorldTime(UDP_StateMachineComponent* OwningComponent) const
{
	if (OwningComponent)
	{
		if (const UWorld* World = OwningComponent->GetWorld())
		{
			return World->GetTimeSeconds();
		}
	}
	return 0.f;
}

void UMove_MovementState::ApplyGroundSpeed(UDP_StateMachineComponent* OwningComponent, float MaxSpeed) const
{
	UCharacterMovementComponent* CMC = GetCMC(OwningComponent);
	if (!CMC)
	{
		return;
	}
	if (MaxSpeed > 0.f)
	{
		CMC->MaxWalkSpeed = MaxSpeed;
	}
	CMC->bOrientRotationToMovement = MotionConfig.bOrientToMovement;
}
