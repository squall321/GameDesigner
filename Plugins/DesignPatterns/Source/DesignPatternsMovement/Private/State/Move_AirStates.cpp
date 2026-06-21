// Copyright DesignPatterns plugin. All Rights Reserved.

#include "State/Move_AirStates.h"
#include "Component/Move_MovementComponent.h"
#include "Data/Move_LocomotionProfile.h"
#include "Settings/Move_DeveloperSettings.h"
#include "Move_NativeTags.h"

#include "FSM/DPBlackboard.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Core/DPLog.h"

// ---- Jump ----

UMove_State_Jump::UMove_State_Jump()
{
	StateTag = MoveNativeTags::State_Jump;
	MotionConfig.Domain = EMove_MotionDomain::Air;
}

void UMove_State_Jump::OnEnter_Implementation(UDP_StateMachineComponent* OwningComponent)
{
	Super::OnEnter_Implementation(OwningComponent);

	UMove_MovementComponent* Move = GetMovementComponent(OwningComponent);
	const UMove_DeveloperSettings* Settings = UMove_DeveloperSettings::Get();
	const UMove_LocomotionProfile* Profile = Move ? Move->GetProfile() : nullptr;

	// Seed the air-jump budget for this airborne sequence.
	int32 Budget = Settings ? Settings->FallbackAirJumpBudget : 1;
	if (Profile && Profile->AirJumpBudget >= 0)
	{
		Budget = Profile->AirJumpBudget;
	}
	if (UDP_Blackboard* BB = OwningComponent ? OwningComponent->GetBlackboard() : nullptr)
	{
		BB->SetInt(Key_AirJumpsRemaining, Budget);
	}

	if (UCharacterMovementComponent* CMC = GetCMC(OwningComponent))
	{
		if (Profile && Profile->AirControl > 0.f)
		{
			CMC->AirControl = Profile->AirControl;
		}
	}

	if (ACharacter* Character = Move ? Move->GetCharacter() : nullptr)
	{
		if (Profile && Profile->JumpZVelocity > 0.f)
		{
			Character->GetCharacterMovement()->JumpZVelocity = Profile->JumpZVelocity;
		}
		Character->Jump();
	}
}

// ---- DoubleJump ----

UMove_State_DoubleJump::UMove_State_DoubleJump()
{
	StateTag = MoveNativeTags::State_DoubleJump;
	MotionConfig.Domain = EMove_MotionDomain::Air;
}

bool UMove_State_DoubleJump::CanEnter_Implementation(UDP_StateMachineComponent* OwningComponent) const
{
	if (const UDP_Blackboard* BB = OwningComponent ? OwningComponent->GetBlackboard() : nullptr)
	{
		return BB->GetInt(Key_AirJumpsRemaining, 0) > 0;
	}
	return false;
}

void UMove_State_DoubleJump::OnEnter_Implementation(UDP_StateMachineComponent* OwningComponent)
{
	Super::OnEnter_Implementation(OwningComponent);

	UMove_MovementComponent* Move = GetMovementComponent(OwningComponent);
	const UMove_LocomotionProfile* Profile = Move ? Move->GetProfile() : nullptr;

	// Single owner of the budget decrement.
	if (UDP_Blackboard* BB = OwningComponent ? OwningComponent->GetBlackboard() : nullptr)
	{
		const int32 Remaining = BB->GetInt(Key_AirJumpsRemaining, 0);
		BB->SetInt(Key_AirJumpsRemaining, FMath::Max(0, Remaining - 1));
	}

	if (UCharacterMovementComponent* CMC = GetCMC(OwningComponent))
	{
		float ZVel = CMC->JumpZVelocity;
		if (Profile && Profile->DoubleJumpZVelocity > 0.f)
		{
			ZVel = Profile->DoubleJumpZVelocity;
		}
		// Reset vertical velocity then launch so the second jump feels consistent regardless of fall speed.
		FVector Vel = CMC->Velocity;
		Vel.Z = ZVel;
		CMC->Velocity = Vel;
		CMC->SetMovementMode(MOVE_Falling);
	}
}

// ---- Dash ----

UMove_State_Dash::UMove_State_Dash()
{
	StateTag = MoveNativeTags::State_Dash;
	MotionConfig.Domain = EMove_MotionDomain::Air;
	MotionConfig.bOrientToMovement = false;
}

void UMove_State_Dash::OnEnter_Implementation(UDP_StateMachineComponent* OwningComponent)
{
	Super::OnEnter_Implementation(OwningComponent);

	UMove_MovementComponent* Move = GetMovementComponent(OwningComponent);
	UCharacterMovementComponent* CMC = GetCMC(OwningComponent);
	if (!Move || !CMC)
	{
		return;
	}

	const UMove_DeveloperSettings* Settings = UMove_DeveloperSettings::Get();
	const float DashSpeed = Settings ? Settings->FallbackDashSpeed : 1500.f;
	const float DashDuration = Settings ? Settings->FallbackDashDuration : 0.2f;

	// Direction: move intent, else current velocity, else facing.
	FVector Dir = Move->GetMoveIntent().GetSafeNormal();
	if (Dir.IsNearlyZero())
	{
		Dir = CMC->Velocity.GetSafeNormal2D();
	}
	if (Dir.IsNearlyZero())
	{
		Dir = Move->GetFacingIntent().Vector().GetSafeNormal2D();
	}

	CMC->Velocity = Dir * DashSpeed;

	// Stamp the dash phase end on the blackboard so OnTick / a guard can end it deterministically.
	if (UDP_Blackboard* BB = OwningComponent->GetBlackboard())
	{
		BB->SetFloat(Key_PhaseEndTime, GetWorldTime(OwningComponent) + DashDuration);
	}
}

void UMove_State_Dash::OnTick_Implementation(UDP_StateMachineComponent* OwningComponent, float DeltaSeconds)
{
	Super::OnTick_Implementation(OwningComponent, DeltaSeconds);
	// Pure translation; the authored Dash->(Walk/Jump/Swim) transition guard ends the dash when
	// world time passes PhaseEndTime. Nothing per-tick needed beyond sustaining velocity (CMC handles it).
}

void UMove_State_Dash::OnExit_Implementation(UDP_StateMachineComponent* OwningComponent)
{
	if (UDP_Blackboard* BB = OwningComponent ? OwningComponent->GetBlackboard() : nullptr)
	{
		BB->ClearKey(Key_PhaseEndTime);
	}
	Super::OnExit_Implementation(OwningComponent);
}
