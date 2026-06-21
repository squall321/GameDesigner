// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Guards/Move_TransitionGuards.h"
#include "Component/Move_MovementComponent.h"
#include "Component/Move_StaminaComponent.h"
#include "State/Move_MovementState.h"
#include "Data/Move_LocomotionProfile.h"
#include "Settings/Move_DeveloperSettings.h"
#include "Trace/Move_TraceLibrary.h"
#include "Move_NativeTags.h"

#include "FSM/DPBlackboard.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"

namespace
{
	const UMove_MovementComponent* AsMove(const UDP_StateMachineComponent* Comp)
	{
		return Cast<UMove_MovementComponent>(Comp);
	}

	// The guard signature is const; intent reads on the seam are logically const but call non-const
	// helpers, so we resolve through a const-cast confined here (the helpers do not mutate FSM state).
	UMove_MovementComponent* AsMoveMutable(const UDP_StateMachineComponent* Comp)
	{
		return const_cast<UMove_MovementComponent*>(Cast<UMove_MovementComponent>(Comp));
	}

	float WorldTime(const UDP_StateMachineComponent* Comp)
	{
		if (Comp)
		{
			if (const UWorld* World = Comp->GetWorld())
			{
				return World->GetTimeSeconds();
			}
		}
		return 0.f;
	}
}

// ---- Airborne ----

bool UMove_AirborneGuard::EvaluateGuard_Implementation(const UDP_StateMachineComponent* OwningComponent) const
{
	const UMove_MovementComponent* Move = AsMove(OwningComponent);
	const UCharacterMovementComponent* CMC = Move ? Move->GetCharacterMovement() : nullptr;
	if (!CMC)
	{
		return false;
	}
	const bool bFalling = CMC->IsFalling();
	return bRequireGrounded ? !bFalling : bFalling;
}

// ---- Intent ----

bool UMove_IntentGuard::EvaluateGuard_Implementation(const UDP_StateMachineComponent* OwningComponent) const
{
	UMove_MovementComponent* Move = AsMoveMutable(OwningComponent);
	if (!Move)
	{
		return false;
	}

	bool bMet = true;
	if (bRequireSprintHeld)
	{
		bMet &= Move->IsSprintHeld();
	}
	if (bRequireCrouchHeld)
	{
		bMet &= Move->IsCrouchHeld();
	}
	if (MinMoveMagnitude > 0.f)
	{
		bMet &= Move->GetMoveIntent().Size() >= MinMoveMagnitude;
	}
	return bInvert ? !bMet : bMet;
}

// ---- Stamina ----

bool UMove_StaminaGuard::EvaluateGuard_Implementation(const UDP_StateMachineComponent* OwningComponent) const
{
	const UMove_MovementComponent* Move = AsMove(OwningComponent);
	if (!Move)
	{
		return false;
	}
	const UMove_StaminaComponent* Stamina = Move->GetStaminaComponent();
	if (!Stamina)
	{
		return true; // unrestricted fallback.
	}
	if (bBlockWhileExhausted && Stamina->IsExhausted())
	{
		return false;
	}
	const float Required = MinStamina > 0.f ? MinStamina : KINDA_SMALL_NUMBER;
	return Stamina->HasStamina(Required);
}

// ---- Ledge available ----

bool UMove_LedgeAvailableGuard::EvaluateGuard_Implementation(const UDP_StateMachineComponent* OwningComponent) const
{
	const UMove_MovementComponent* Move = AsMove(OwningComponent);
	const ACharacter* Character = Move ? Move->GetCharacter() : nullptr;
	if (!Character)
	{
		return false;
	}

	const UMove_DeveloperSettings* Settings = UMove_DeveloperSettings::Get();
	const UMove_LocomotionProfile* Profile = Move->GetProfile();

	FMove_LedgeTuning Tuning;
	Tuning.ForwardReach = Settings ? Settings->FallbackLedgeReach : 80.f;
	Tuning.MaxMantleHeight = Settings ? Settings->FallbackMaxMantleHeight : 200.f;
	Tuning.VaultMaxHeight = Settings ? Settings->FallbackVaultMaxHeight : 90.f;
	if (Profile)
	{
		Tuning.Channels = Profile->TraversalTraceChannels;
	}

	const FMove_LedgeResult Result = UMove_TraceLibrary::FindLedge(Character, Tuning);
	if (!Result.bFound)
	{
		return false;
	}
	switch (LedgeKindFilter)
	{
	case 1: return Result.bIsVault;   // vault-only
	case 2: return !Result.bIsVault;  // mantle-only
	default: return true;             // any
	}
}

// ---- Phase elapsed ----

bool UMove_PhaseElapsedGuard::EvaluateGuard_Implementation(const UDP_StateMachineComponent* OwningComponent) const
{
	const UDP_Blackboard* BB = OwningComponent ? OwningComponent->GetBlackboard() : nullptr;
	if (!BB)
	{
		return !bInvert; // no timer -> treat as elapsed.
	}
	const float End = BB->GetFloat(UMove_MovementState::Key_PhaseEndTime, 0.f);
	const bool bElapsed = WorldTime(OwningComponent) >= End;
	return bInvert ? !bElapsed : bElapsed;
}

// ---- Traversal complete ----

bool UMove_TraversalCompleteGuard::EvaluateGuard_Implementation(const UDP_StateMachineComponent* OwningComponent) const
{
	const UDP_Blackboard* BB = OwningComponent ? OwningComponent->GetBlackboard() : nullptr;
	if (!BB)
	{
		return true;
	}
	const float Start = BB->GetFloat(UMove_MovementState::Key_TraversalStartTime, 0.f);
	const float Duration = FMath::Max(BB->GetFloat(UMove_MovementState::Key_TraversalDuration, 0.45f), KINDA_SMALL_NUMBER);
	return (WorldTime(OwningComponent) - Start) >= Duration;
}

// ---- Special move confirmed ----

bool UMove_SpecialMoveConfirmedGuard::EvaluateGuard_Implementation(const UDP_StateMachineComponent* OwningComponent) const
{
	const UDP_Blackboard* BB = OwningComponent ? OwningComponent->GetBlackboard() : nullptr;
	if (!BB)
	{
		return false;
	}
	// The intent component encodes the server-confirmed request tag as its hash under the confirmed key
	// (a UDP_Blackboard stores ints natively, and the token must survive without a UObject ref).
	const int32 ConfirmedHash = BB->GetInt(UMove_MovementState::Key_SpecialMoveConfirmed, 0);
	const int32 WantHash = RequestTag.IsValid() ? static_cast<int32>(GetTypeHash(RequestTag)) : 0;
	return ConfirmedHash != 0 && ConfirmedHash == WantHash;
}

// ---- In water ----

bool UMove_InWaterGuard::EvaluateGuard_Implementation(const UDP_StateMachineComponent* OwningComponent) const
{
	const UMove_MovementComponent* Move = AsMove(OwningComponent);
	const bool bInWater = Move && UMove_TraceLibrary::IsInWater(Move->GetCharacter());
	return bRequireOutOfWater ? !bInWater : bInWater;
}
