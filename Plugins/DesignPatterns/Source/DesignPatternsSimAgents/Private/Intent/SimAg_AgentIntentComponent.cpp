// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Intent/SimAg_AgentIntentComponent.h"
#include "Brain/SimAg_AgentComponent.h"
#include "Jobs/SimAg_JobBoardSubsystem.h"
#include "Jobs/SimAg_JobReservationSubsystem.h"
#include "Jobs/Seam_JobReservation.h"
#include "DesignPatternsSimAgentsTags.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"

USimAg_AgentIntentComponent::USimAg_AgentIntentComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	// Replicated only so the Server RPCs route; no replicated authoritative state lives here.
	SetIsReplicatedByDefault(true);
}

FSeam_EntityId USimAg_AgentIntentComponent::ResolveOwnerAgentId() const
{
	if (const AActor* Owner = GetOwner())
	{
		if (const USimAg_AgentComponent* Agent = Owner->FindComponentByClass<USimAg_AgentComponent>())
		{
			return Agent->GetAgentId();
		}
	}
	return FSeam_EntityId::Invalid();
}

//~ Server_RequestClaimJob ------------------------------------------------------------------------

bool USimAg_AgentIntentComponent::Server_RequestClaimJob_Validate(FGameplayTag JobKind, FVector /*Location*/)
{
	// Reject garbage: an invalid job kind, or a request from an actor with no agent identity.
	return JobKind.IsValid() && ResolveOwnerAgentId().IsValid();
}

void USimAg_AgentIntentComponent::Server_RequestClaimJob_Implementation(FGameplayTag JobKind, FVector Location)
{
	// AUTHORITY GUARD at top (defensive: a Server RPC should already be on the server).
	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FSeam_EntityId AgentId = ResolveOwnerAgentId();
	if (!AgentId.IsValid())
	{
		return; // never trust a client-sent id; we derive it and bail if there isn't one
	}

	if (USimAg_JobBoardSubsystem* Board = World->GetSubsystem<USimAg_JobBoardSubsystem>())
	{
		const FSimAg_JobHandle Claimed = Board->ClaimJobForAgent(JobKind, Location, AgentId);
		if (Claimed.IsValid())
		{
			// Mirror the claim onto the agent so its brain/steering pick it up authoritatively.
			if (USimAg_AgentComponent* Agent = Owner->FindComponentByClass<USimAg_AgentComponent>())
			{
				Agent->SetClaimedJob(Claimed);
			}
		}
	}
}

//~ Server_RequestReserve -------------------------------------------------------------------------

bool USimAg_AgentIntentComponent::Server_RequestReserve_Validate(FSeam_EntityId Target)
{
	return Target.IsValid() && ResolveOwnerAgentId().IsValid();
}

void USimAg_AgentIntentComponent::Server_RequestReserve_Implementation(FSeam_EntityId Target)
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}

	const FSeam_EntityId AgentId = ResolveOwnerAgentId();
	if (!AgentId.IsValid())
	{
		return;
	}

	// Resolve the reservation seam via the service locator (never a concrete include beyond the seam type).
	if (UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		if (UObject* Service = Locator->ResolveService(SimAgNativeTags::Service_JobReservation))
		{
			if (Service->Implements<USeam_JobReservation>())
			{
				ISeam_JobReservation::Execute_TryReserve(Service, Target, AgentId);
			}
		}
	}
}

//~ Server_RequestRelease -------------------------------------------------------------------------

bool USimAg_AgentIntentComponent::Server_RequestRelease_Validate(FSeam_EntityId Target)
{
	return Target.IsValid() && ResolveOwnerAgentId().IsValid();
}

void USimAg_AgentIntentComponent::Server_RequestRelease_Implementation(FSeam_EntityId Target)
{
	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}
	const FSeam_EntityId AgentId = ResolveOwnerAgentId();
	if (!AgentId.IsValid())
	{
		return;
	}

	if (UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		if (UObject* Service = Locator->ResolveService(SimAgNativeTags::Service_JobReservation))
		{
			if (USimAg_JobReservationSubsystem* Reservation = Cast<USimAg_JobReservationSubsystem>(Service))
			{
				// Only release a reservation this agent actually holds (don't let one agent free another's).
				if (Reservation->GetReservationHolder(Target) == AgentId)
				{
					ISeam_JobReservation::Execute_Release(Service, Target);
				}
			}
		}
	}
}
