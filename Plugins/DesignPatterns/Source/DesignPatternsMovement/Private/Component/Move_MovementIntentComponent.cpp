// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Component/Move_MovementIntentComponent.h"
#include "Component/Move_MovementComponent.h"
#include "Component/Move_StaminaComponent.h"
#include "State/Move_MovementState.h"
#include "Data/Move_LocomotionProfile.h"
#include "Settings/Move_DeveloperSettings.h"
#include "Trace/Move_TraceLibrary.h"
#include "Move_NativeTags.h"

#include "FSM/DPBlackboard.h"
#include "Action/DPGameplayActionComponent.h"

#include "GameFramework/Character.h"
#include "GameFramework/Actor.h"
#include "Core/DPLog.h"

UMove_MovementIntentComponent::UMove_MovementIntentComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	// No replicated state of its own; intent is local, special-move admission flows through RPCs + the
	// FSM blackboard token. Replication is not enabled here.
}

void UMove_MovementIntentComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!HasValidNetOwnership() && !bLoggedOwnershipWarning)
	{
		bLoggedOwnershipWarning = true;
		UE_LOG(LogDPFSM, Warning,
			TEXT("[Movement] %s on %s lacks a net-owning connection; special-move server RPCs will not "
			     "route. Place this component on the player-controlled pawn."),
			*GetName(), GetOwner() ? *GetOwner()->GetName() : TEXT("<null>"));
	}
}

// ---- ISeam_MovementController ----

bool UMove_MovementIntentComponent::ConsumeSpecialMoveRequest_Implementation(FGameplayTag& OutRequestTag)
{
	if (PendingRequest.IsValid())
	{
		OutRequestTag = PendingRequest;
		PendingRequest = FGameplayTag();
		return true;
	}
	return false;
}

// ---- Requesting ----

void UMove_MovementIntentComponent::RequestSpecialMove(FGameplayTag RequestTag)
{
	if (!RequestTag.IsValid())
	{
		return;
	}

	// Local one-shot for immediate cosmetic prediction (the movement component consumes it this tick).
	PendingRequest = RequestTag;

	// Authoritative path: send to the server with a predicted context (no payload needed for simple
	// moves; mantle/vault carry the predicted target so the server can sanity-bound it, but it re-traces).
	FSeam_NetValue Context;
	if (const ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		Context = FSeam_NetValue::MakeVector(Character->GetActorLocation());
	}

	if (GetOwnerRole() == ROLE_Authority)
	{
		// Listen server / standalone: apply directly (still re-derives pre-conditions).
		if (ServerRequestSpecialMove_Validate(RequestTag, Context))
		{
			ApplyAuthoritativeSpecialMove(RequestTag);
		}
	}
	else if (HasValidNetOwnership())
	{
		ServerRequestSpecialMove(RequestTag, Context);
	}
}

bool UMove_MovementIntentComponent::ServerRequestSpecialMove_Validate(FGameplayTag RequestTag, FSeam_NetValue Context)
{
	// Cheap structural validation; deep re-derivation happens in the implementation so a failed gameplay
	// check does not disconnect the client (it just no-ops).
	return RequestTag.IsValid() && RequestToStateTag(RequestTag).IsValid();
}

void UMove_MovementIntentComponent::ServerRequestSpecialMove_Implementation(FGameplayTag RequestTag, FSeam_NetValue Context)
{
	// Authority guard at the TOP (this mutates authoritative FSM state).
	if (GetOwnerRole() != ROLE_Authority)
	{
		return;
	}

	// --- Re-derive pre-conditions the client cannot be trusted on ---

	// 1) Stamina (dash/dodge): server reads its OWN stamina meter.
	const bool bIsDash = (RequestTag == MoveNativeTags::Request_Dash) || (RequestTag == MoveNativeTags::Request_Dodge);
	if (bIsDash)
	{
		if (UMove_StaminaComponent* Stamina = ResolveStaminaComponent())
		{
			if (Stamina->IsExhausted() || !Stamina->HasStamina(Stamina->GetDashCost()))
			{
				UE_LOG(LogDPFSM, Verbose, TEXT("[Movement] Server rejected dash: insufficient stamina."));
				return;
			}
		}
	}

	// 2) Cooldown (any action-backed move): if a matching granted action exists, require it ready.
	if (UDP_GameplayActionComponent* ActionComp = ResolveActionComponent())
	{
		if (ActionComp->HasActionWithTag(RequestTag))
		{
			// A granted action exists for this request — let the action component's gate run it so its
			// cooldown is authoritative. If it cannot activate (cooldown), bail.
			FDP_ActionActivationData Data;
			Data.Instigator = GetOwner();
			Data.SourceComponent = ActionComp;
			if (!ActionComp->ActivateActionByTag(RequestTag, Data))
			{
				UE_LOG(LogDPFSM, Verbose, TEXT("[Movement] Server rejected %s: action on cooldown/blocked."),
					*RequestTag.ToString());
				return;
			}
		}
	}

	// 3) Traversal moves: re-run the trace SERVER-SIDE and write the authoritative target transform.
	const bool bIsTraversal = (RequestTag == MoveNativeTags::Request_Mantle) || (RequestTag == MoveNativeTags::Request_Vault);
	if (bIsTraversal)
	{
		UMove_MovementComponent* Move = ResolveMovementComponent();
		ACharacter* Character = Move ? Move->GetCharacter() : nullptr;
		if (!Move || !Character)
		{
			return;
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

		const FMove_LedgeResult Ledge = UMove_TraceLibrary::FindLedge(Character, Tuning);
		if (!Ledge.bFound)
		{
			UE_LOG(LogDPFSM, Verbose, TEXT("[Movement] Server rejected traversal: no ledge found."));
			return;
		}
		// Reject a mismatched request (client asked to vault a tall ledge, or mantle a low one).
		const bool bWantVault = (RequestTag == MoveNativeTags::Request_Vault);
		if (bWantVault != Ledge.bIsVault)
		{
			// Remap to the server's verdict so the correct state is driven.
			RequestTag = Ledge.bIsVault ? MoveNativeTags::Request_Vault : MoveNativeTags::Request_Mantle;
		}

		// Write the authoritative target onto the FSM blackboard for the traversal state to interpolate to.
		if (UDP_Blackboard* BB = Move->GetBlackboard())
		{
			BB->SetVector(UMove_MovementState::Key_TraversalTargetLocation, Ledge.TargetTransform.GetLocation());
		}
	}

	ApplyAuthoritativeSpecialMove(RequestTag);
}

void UMove_MovementIntentComponent::ApplyAuthoritativeSpecialMove(FGameplayTag RequestTag)
{
	UMove_MovementComponent* Move = ResolveMovementComponent();
	if (!Move)
	{
		return;
	}
	const FGameplayTag StateTag = RequestToStateTag(RequestTag);
	if (!StateTag.IsValid())
	{
		return;
	}

	// Stamp the server-confirmed token so the confirmed-guard admits the transition, then drive entry.
	if (UDP_Blackboard* BB = Move->GetBlackboard())
	{
		BB->SetInt(UMove_MovementState::Key_SpecialMoveConfirmed, static_cast<int32>(GetTypeHash(RequestTag)));
		// Mark traversing for mantle/vault so a second request cannot interrupt the interpolation.
		if (StateTag == MoveNativeTags::State_Mantle || StateTag == MoveNativeTags::State_Vault)
		{
			if (UDP_GameplayActionComponent* ActionComp = ResolveActionComponent())
			{
				ActionComp->AddOwnedTag(MoveNativeTags::Status_Traversing);
			}
		}
	}

	// Authority-side state change (replicates the new ActiveStateTag to clients via the base FSM).
	Move->ChangeState(StateTag, /*bForce*/false);

	// Clear the confirmed token after driving entry; the target state has already keyed off it.
	if (UDP_Blackboard* BB = Move->GetBlackboard())
	{
		BB->SetInt(UMove_MovementState::Key_SpecialMoveConfirmed, 0);
	}
}

// ---- Resolution helpers ----

UMove_MovementComponent* UMove_MovementIntentComponent::ResolveMovementComponent() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<UMove_MovementComponent>() : nullptr;
}

UMove_StaminaComponent* UMove_MovementIntentComponent::ResolveStaminaComponent() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<UMove_StaminaComponent>() : nullptr;
}

UDP_GameplayActionComponent* UMove_MovementIntentComponent::ResolveActionComponent() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<UDP_GameplayActionComponent>() : nullptr;
}

bool UMove_MovementIntentComponent::HasValidNetOwnership() const
{
	const AActor* Owner = GetOwner();
	// A connection-owning actor (the player's pawn possessed by a PlayerController) can route Server RPCs.
	return Owner && Owner->GetNetConnection() != nullptr;
}

FGameplayTag UMove_MovementIntentComponent::RequestToStateTag(const FGameplayTag& RequestTag)
{
	if (RequestTag == MoveNativeTags::Request_Dash || RequestTag == MoveNativeTags::Request_Dodge)
	{
		return MoveNativeTags::State_Dash;
	}
	if (RequestTag == MoveNativeTags::Request_Mantle)
	{
		return MoveNativeTags::State_Mantle;
	}
	if (RequestTag == MoveNativeTags::Request_Vault)
	{
		return MoveNativeTags::State_Vault;
	}
	if (RequestTag == MoveNativeTags::Request_Jump)
	{
		return MoveNativeTags::State_Jump;
	}
	if (RequestTag == MoveNativeTags::Request_WallRun)
	{
		return MoveNativeTags::State_WallRun;
	}
	if (RequestTag == MoveNativeTags::Request_Climb)
	{
		return MoveNativeTags::State_Climb;
	}
	return FGameplayTag();
}
