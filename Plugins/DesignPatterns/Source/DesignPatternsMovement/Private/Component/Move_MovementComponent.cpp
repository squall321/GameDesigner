// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Component/Move_MovementComponent.h"
#include "Component/Move_StaminaComponent.h"
#include "Data/Move_LocomotionProfile.h"

#include "Move/Seam_MovementController.h"

#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "Core/DPLog.h"

UMove_MovementComponent::UMove_MovementComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	SetIsReplicatedByDefault(true); // inherits ActiveStateTag replication from the base FSM component.

	// Movement FSMs are tick-driven by default (the base already defaults bEvaluateTransitionsOnTick).
}

void UMove_MovementComponent::BeginPlay()
{
	CacheOwnerObjects();
	Super::BeginPlay(); // base resolves the Definition and enters the initial state (which reads our cache).
}

void UMove_MovementComponent::CacheOwnerObjects()
{
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		CachedCharacter = Character;
		CachedCMC = Character->GetCharacterMovement();
	}
	else
	{
		UE_LOG(LogDPFSM, Warning,
			TEXT("[Movement] %s owner is not an ACharacter; movement states will no-op."),
			*GetName());
	}

	if (AActor* Owner = GetOwner())
	{
		CachedStamina = Owner->FindComponentByClass<UMove_StaminaComponent>();
	}
}

ACharacter* UMove_MovementComponent::GetCharacter() const
{
	if (ACharacter* Cached = CachedCharacter.Get())
	{
		return Cached;
	}
	return Cast<ACharacter>(GetOwner());
}

UCharacterMovementComponent* UMove_MovementComponent::GetCharacterMovement() const
{
	if (UCharacterMovementComponent* Cached = CachedCMC.Get())
	{
		return Cached;
	}
	if (ACharacter* Character = GetCharacter())
	{
		return Character->GetCharacterMovement();
	}
	return nullptr;
}

UMove_StaminaComponent* UMove_MovementComponent::GetStaminaComponent() const
{
	if (UMove_StaminaComponent* Cached = CachedStamina.Get())
	{
		return Cached;
	}
	if (const AActor* Owner = GetOwner())
	{
		return Owner->FindComponentByClass<UMove_StaminaComponent>();
	}
	return nullptr;
}

TScriptInterface<ISeam_MovementController> UMove_MovementComponent::GetMovementController()
{
	auto Wrap = [](UObject* Obj) -> TScriptInterface<ISeam_MovementController>
	{
		TScriptInterface<ISeam_MovementController> Result;
		if (Obj && Obj->GetClass()->ImplementsInterface(USeam_MovementController::StaticClass()))
		{
			Result.SetObject(Obj);
			Result.SetInterface(Cast<ISeam_MovementController>(Obj));
		}
		return Result;
	};

	// Fast path: cached provider still valid.
	if (UObject* Cached = CachedControllerObject.Get())
	{
		TScriptInterface<ISeam_MovementController> Cur = Wrap(Cached);
		if (Cur.GetObject())
		{
			return Cur;
		}
	}

	// Resolution order: the Pawn's controller, then a co-located component, then the owner actor itself.
	if (ACharacter* Character = GetCharacter())
	{
		if (AController* Controller = Character->GetController())
		{
			if (TScriptInterface<ISeam_MovementController> FromController = Wrap(Controller); FromController.GetObject())
			{
				CachedControllerObject = Controller;
				return FromController;
			}
			// A controller-owned component (e.g. a player input driver) may implement the seam.
			if (UActorComponent* Comp = Controller->FindComponentByInterface(USeam_MovementController::StaticClass()))
			{
				CachedControllerObject = Comp;
				return Wrap(Comp);
			}
		}

		if (UActorComponent* Comp = Character->FindComponentByInterface(USeam_MovementController::StaticClass()))
		{
			CachedControllerObject = Comp;
			return Wrap(Comp);
		}

		if (TScriptInterface<ISeam_MovementController> FromActor = Wrap(Character); FromActor.GetObject())
		{
			CachedControllerObject = Character;
			return FromActor;
		}
	}

	return TScriptInterface<ISeam_MovementController>();
}

FVector UMove_MovementComponent::GetMoveIntent()
{
	if (TScriptInterface<ISeam_MovementController> Controller = GetMovementController())
	{
		return ISeam_MovementController::Execute_GetMoveIntent(Controller.GetObject());
	}
	return FVector::ZeroVector;
}

bool UMove_MovementComponent::IsSprintHeld()
{
	if (TScriptInterface<ISeam_MovementController> Controller = GetMovementController())
	{
		return ISeam_MovementController::Execute_IsSprintHeld(Controller.GetObject());
	}
	return false;
}

bool UMove_MovementComponent::IsCrouchHeld()
{
	if (TScriptInterface<ISeam_MovementController> Controller = GetMovementController())
	{
		return ISeam_MovementController::Execute_IsCrouchHeld(Controller.GetObject());
	}
	return false;
}

FRotator UMove_MovementComponent::GetFacingIntent()
{
	if (TScriptInterface<ISeam_MovementController> Controller = GetMovementController())
	{
		return ISeam_MovementController::Execute_GetFacingIntent(Controller.GetObject());
	}
	if (const AActor* Owner = GetOwner())
	{
		return Owner->GetActorRotation();
	}
	return FRotator::ZeroRotator;
}

bool UMove_MovementComponent::ConsumeSpecialMoveRequest(FGameplayTag& OutRequestTag)
{
	if (TScriptInterface<ISeam_MovementController> Controller = GetMovementController())
	{
		return ISeam_MovementController::Execute_ConsumeSpecialMoveRequest(Controller.GetObject(), OutRequestTag);
	}
	return false;
}
