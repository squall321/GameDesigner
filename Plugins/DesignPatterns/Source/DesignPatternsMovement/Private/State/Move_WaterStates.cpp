// Copyright DesignPatterns plugin. All Rights Reserved.

#include "State/Move_WaterStates.h"
#include "Component/Move_MovementComponent.h"
#include "Data/Move_LocomotionProfile.h"
#include "Settings/Move_DeveloperSettings.h"
#include "Trace/Move_TraceLibrary.h"
#include "Move_NativeTags.h"

#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

UMove_State_Swim::UMove_State_Swim()
{
	StateTag = MoveNativeTags::State_Swim;
	MotionConfig.Domain = EMove_MotionDomain::Water;
}

bool UMove_State_Swim::CanEnter_Implementation(UDP_StateMachineComponent* OwningComponent) const
{
	const UMove_MovementComponent* Move = GetMovementComponent(OwningComponent);
	return Move && UMove_TraceLibrary::IsInWater(Move->GetCharacter());
}

void UMove_State_Swim::OnEnter_Implementation(UDP_StateMachineComponent* OwningComponent)
{
	Super::OnEnter_Implementation(OwningComponent);

	UMove_MovementComponent* Move = GetMovementComponent(OwningComponent);
	UCharacterMovementComponent* CMC = GetCMC(OwningComponent);
	if (!Move || !CMC)
	{
		return;
	}

	const UMove_DeveloperSettings* Settings = UMove_DeveloperSettings::Get();
	const UMove_LocomotionProfile* Profile = Move->GetProfile();
	float SwimSpeed = Settings ? Settings->FallbackSwimSpeed : 300.f;
	if (Profile && Profile->Swim.MaxSpeed > 0.f)
	{
		SwimSpeed = Profile->Swim.MaxSpeed;
	}

	CMC->MaxSwimSpeed = SwimSpeed;
	CMC->SetMovementMode(MOVE_Swimming);
}

void UMove_State_Swim::OnExit_Implementation(UDP_StateMachineComponent* OwningComponent)
{
	if (UCharacterMovementComponent* CMC = GetCMC(OwningComponent))
	{
		// Leave swimming only if we are no longer in water; otherwise the CMC keeps swimming naturally.
		if (CMC->MovementMode == MOVE_Swimming)
		{
			CMC->SetMovementMode(MOVE_Walking);
		}
	}
	Super::OnExit_Implementation(OwningComponent);
}
