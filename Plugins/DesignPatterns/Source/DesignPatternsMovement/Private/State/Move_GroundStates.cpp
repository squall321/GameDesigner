// Copyright DesignPatterns plugin. All Rights Reserved.

#include "State/Move_GroundStates.h"
#include "Component/Move_MovementComponent.h"
#include "Component/Move_StaminaComponent.h"
#include "Data/Move_LocomotionProfile.h"
#include "Settings/Move_DeveloperSettings.h"
#include "Move_NativeTags.h"

#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Core/DPLog.h"

namespace
{
	/** Resolve a domain MaxSpeed from the profile or settings fallback (documented defensive default). */
	float ResolveSpeed(const UMove_LocomotionProfile* Profile, const FMove_DomainTuning* Row, float Fallback)
	{
		if (Profile && Row && Row->MaxSpeed > 0.f)
		{
			return Row->MaxSpeed;
		}
		return Fallback;
	}
}

// ---- Walk ----

UMove_State_Walk::UMove_State_Walk()
{
	StateTag = MoveNativeTags::State_Walk;
	MotionConfig.Domain = EMove_MotionDomain::Ground;
}

void UMove_State_Walk::OnEnter_Implementation(UDP_StateMachineComponent* OwningComponent)
{
	Super::OnEnter_Implementation(OwningComponent);

	UMove_MovementComponent* Move = GetMovementComponent(OwningComponent);
	const UMove_DeveloperSettings* Settings = UMove_DeveloperSettings::Get();
	const UMove_LocomotionProfile* Profile = Move ? Move->GetProfile() : nullptr;

	const float Speed = ResolveSpeed(Profile, Profile ? &Profile->Walk : nullptr,
		Settings ? Settings->FallbackWalkSpeed : 200.f);
	ApplyGroundSpeed(OwningComponent, Speed);
}

void UMove_State_Walk::OnTick_Implementation(UDP_StateMachineComponent* OwningComponent, float DeltaSeconds)
{
	// Transitions are evaluated by the FSM each tick; Walk has no per-tick work beyond the base strategy.
	Super::OnTick_Implementation(OwningComponent, DeltaSeconds);
}

// ---- Run ----

UMove_State_Run::UMove_State_Run()
{
	StateTag = MoveNativeTags::State_Run;
	MotionConfig.Domain = EMove_MotionDomain::Ground;
}

void UMove_State_Run::OnEnter_Implementation(UDP_StateMachineComponent* OwningComponent)
{
	Super::OnEnter_Implementation(OwningComponent);

	UMove_MovementComponent* Move = GetMovementComponent(OwningComponent);
	const UMove_DeveloperSettings* Settings = UMove_DeveloperSettings::Get();
	const UMove_LocomotionProfile* Profile = Move ? Move->GetProfile() : nullptr;

	const float Speed = ResolveSpeed(Profile, Profile ? &Profile->Run : nullptr,
		Settings ? Settings->FallbackRunSpeed : 400.f);
	ApplyGroundSpeed(OwningComponent, Speed);
}

// ---- Sprint ----

UMove_State_Sprint::UMove_State_Sprint()
{
	StateTag = MoveNativeTags::State_Sprint;
	MotionConfig.Domain = EMove_MotionDomain::Ground;
}

bool UMove_State_Sprint::CanEnter_Implementation(UDP_StateMachineComponent* OwningComponent) const
{
	// Block sprint entry without stamina / while exhausted. Clients answer this from the replicated meter.
	if (const UMove_MovementComponent* Move = GetMovementComponent(OwningComponent))
	{
		if (const UMove_StaminaComponent* Stamina = Move->GetStaminaComponent())
		{
			return !Stamina->IsExhausted() && Stamina->HasStamina(KINDA_SMALL_NUMBER);
		}
	}
	// No stamina component -> sprint is unrestricted (documented fallback).
	return true;
}

void UMove_State_Sprint::OnEnter_Implementation(UDP_StateMachineComponent* OwningComponent)
{
	Super::OnEnter_Implementation(OwningComponent);

	UMove_MovementComponent* Move = GetMovementComponent(OwningComponent);
	const UMove_DeveloperSettings* Settings = UMove_DeveloperSettings::Get();
	const UMove_LocomotionProfile* Profile = Move ? Move->GetProfile() : nullptr;

	const float Speed = ResolveSpeed(Profile, Profile ? &Profile->Sprint : nullptr,
		Settings ? Settings->FallbackSprintSpeed : 650.f);
	ApplyGroundSpeed(OwningComponent, Speed);
}

void UMove_State_Sprint::OnTick_Implementation(UDP_StateMachineComponent* OwningComponent, float DeltaSeconds)
{
	Super::OnTick_Implementation(OwningComponent, DeltaSeconds);

	UMove_MovementComponent* Move = GetMovementComponent(OwningComponent);
	if (!Move)
	{
		return;
	}

	// Stamina drain is AUTHORITATIVE state — guard at the top. Clients let the replicated value flow in.
	const ACharacter* Character = Move->GetCharacter();
	if (!Character || Character->GetLocalRole() != ROLE_Authority)
	{
		return;
	}

	UMove_StaminaComponent* Stamina = Move->GetStaminaComponent();
	if (!Stamina)
	{
		return; // unrestricted sprint when no stamina meter is present.
	}

	const UMove_DeveloperSettings* Settings = UMove_DeveloperSettings::Get();
	float DrainPerSecond = Settings ? Settings->FallbackSprintDrainPerSecond : 10.f;
	if (Stamina->Profile && Stamina->Profile->SprintDrainPerSecond > 0.f)
	{
		DrainPerSecond = Stamina->Profile->SprintDrainPerSecond;
	}

	// Mark draining (resets regen window) and consume; sim-clock scaling lives in the component, but the
	// drain amount itself is per-frame here. The component's TryDrain clamps and latches exhaustion.
	Stamina->NotifyDraining();
	Stamina->TryDrain(DrainPerSecond * DeltaSeconds);
	// The FSM's authored transitions (Sprint -> Run when stamina depleted / sprint released) take it out.
}

// ---- Crouch ----

UMove_State_Crouch::UMove_State_Crouch()
{
	StateTag = MoveNativeTags::State_Crouch;
	MotionConfig.Domain = EMove_MotionDomain::Ground;
}

void UMove_State_Crouch::OnEnter_Implementation(UDP_StateMachineComponent* OwningComponent)
{
	Super::OnEnter_Implementation(OwningComponent);

	UMove_MovementComponent* Move = GetMovementComponent(OwningComponent);
	if (ACharacter* Character = Move ? Move->GetCharacter() : nullptr)
	{
		// Crouch is engine-driven; respects the character's CanCrouch flag.
		Character->Crouch();
	}

	const UMove_DeveloperSettings* Settings = UMove_DeveloperSettings::Get();
	const UMove_LocomotionProfile* Profile = Move ? Move->GetProfile() : nullptr;
	const float Speed = ResolveSpeed(Profile, Profile ? &Profile->Crouch : nullptr,
		Settings ? Settings->FallbackCrouchSpeed : 150.f);

	if (UCharacterMovementComponent* CMC = GetCMC(OwningComponent))
	{
		if (Speed > 0.f)
		{
			CMC->MaxWalkSpeedCrouched = Speed;
		}
	}
}

void UMove_State_Crouch::OnExit_Implementation(UDP_StateMachineComponent* OwningComponent)
{
	if (UMove_MovementComponent* Move = GetMovementComponent(OwningComponent))
	{
		if (ACharacter* Character = Move->GetCharacter())
		{
			Character->UnCrouch();
		}
	}
	Super::OnExit_Implementation(OwningComponent);
}

// ---- Slide ----

UMove_State_Slide::UMove_State_Slide()
{
	StateTag = MoveNativeTags::State_Slide;
	MotionConfig.Domain = EMove_MotionDomain::Ground;
	MotionConfig.bOrientToMovement = false; // a slide keeps its entry direction.
}

void UMove_State_Slide::OnEnter_Implementation(UDP_StateMachineComponent* OwningComponent)
{
	Super::OnEnter_Implementation(OwningComponent);

	if (UMove_MovementComponent* Move = GetMovementComponent(OwningComponent))
	{
		if (ACharacter* Character = Move->GetCharacter())
		{
			Character->Crouch(); // a slide is a low-profile crouch with momentum.
		}
	}
	if (UCharacterMovementComponent* CMC = GetCMC(OwningComponent))
	{
		// Allow the slide to carry speed above the crouch cap by temporarily lifting it.
		CMC->bOrientRotationToMovement = false;
	}
}

void UMove_State_Slide::OnTick_Implementation(UDP_StateMachineComponent* OwningComponent, float DeltaSeconds)
{
	Super::OnTick_Implementation(OwningComponent, DeltaSeconds);

	UMove_MovementComponent* Move = GetMovementComponent(OwningComponent);
	UCharacterMovementComponent* CMC = GetCMC(OwningComponent);
	if (!Move || !CMC)
	{
		return;
	}

	const UMove_DeveloperSettings* Settings = UMove_DeveloperSettings::Get();
	const UMove_LocomotionProfile* Profile = Move->GetProfile();

	float SlopeThresholdDeg = Settings ? Settings->FallbackSlideSlopeThresholdDeg : 15.f;
	if (Profile && Profile->SlideSlopeThresholdDeg > 0.f)
	{
		SlopeThresholdDeg = Profile->SlideSlopeThresholdDeg;
	}
	const float SlopeAccel = Profile ? Profile->SlideSlopeAcceleration : 800.f;

	// Slope-aware acceleration: if the floor is steeper than the threshold, push downhill.
	const FFindFloorResult& Floor = CMC->CurrentFloor;
	if (Floor.bBlockingHit)
	{
		const FVector Normal = Floor.HitResult.ImpactNormal;
		const float SlopeDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(Normal.Z, -1.f, 1.f)));
		if (SlopeDeg >= SlopeThresholdDeg)
		{
			// Downhill direction = gravity projected onto the floor plane.
			FVector Downhill = FVector::VectorPlaneProject(FVector::DownVector, Normal).GetSafeNormal();
			const float SteepnessAlpha = FMath::Clamp((SlopeDeg - SlopeThresholdDeg) / 45.f, 0.f, 1.f);
			CMC->AddInputVector(Downhill * SteepnessAlpha);
			// Manual downhill velocity nudge so the slide builds speed even with low input scale.
			CMC->Velocity += Downhill * SlopeAccel * SteepnessAlpha * DeltaSeconds;
		}
	}
	// Self-exit (below min speed) is enforced by the authored Slide->Crouch/Walk transition guard.
}

void UMove_State_Slide::OnExit_Implementation(UDP_StateMachineComponent* OwningComponent)
{
	if (UMove_MovementComponent* Move = GetMovementComponent(OwningComponent))
	{
		if (ACharacter* Character = Move->GetCharacter())
		{
			Character->UnCrouch();
		}
	}
	Super::OnExit_Implementation(OwningComponent);
}
