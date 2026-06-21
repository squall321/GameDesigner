// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Ride/WS_RiderComponent.h"
#include "Ride/WS_RideableComponent.h"
#include "Core/DPLog.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"

UWS_RiderComponent::UWS_RiderComponent()
{
	// Lives on a player-owned pawn; replicates a small owner-only mirror of the current ride.
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = false;
}

void UWS_RiderComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Only the owning client needs the ride mirror (it drives that client's UI/camera). Other clients
	// observe the rider via the rideable's seat attachment, not this component.
	FDoRepLifetimeParams OwnerOnly;
	OwnerOnly.Condition = COND_OwnerOnly;
	DOREPLIFETIME_WITH_PARAMS_FAST(UWS_RiderComponent, CurrentRideable, OwnerOnly);
	DOREPLIFETIME_WITH_PARAMS_FAST(UWS_RiderComponent, CurrentSeatIndex, OwnerOnly);
}

void UWS_RiderComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// If the pawn is torn down mid-ride on the server, free the seat and reverse possession cleanly.
	if (GetOwner() && GetOwner()->HasAuthority() && CurrentRideable.IsValid())
	{
		AuthExit();
	}
	Super::EndPlay(EndPlayReason);
}

//~ Owner / pawn helpers ------------------------------------------------------------------------

APawn* UWS_RiderComponent::GetOwnerPawn() const
{
	return Cast<APawn>(GetOwner());
}

AController* UWS_RiderComponent::GetOwnerController() const
{
	const APawn* Pawn = GetOwnerPawn();
	return Pawn ? Pawn->GetController() : nullptr;
}

float UWS_RiderComponent::ApproxRadius(const AActor* Actor)
{
	if (!Actor)
	{
		return 0.f;
	}
	// Sphere bounds give a cheap, animation-independent radius for a proximity gate.
	FVector Origin, Extent;
	Actor->GetActorBounds(/*bOnlyCollidingComponents*/ true, Origin, Extent);
	return Extent.GetMax();
}

//~ Client request API --------------------------------------------------------------------------

bool UWS_RiderComponent::RequestEnter(UWS_RideableComponent* Rideable, int32 PreferredSeatIndex)
{
	// Cheap local pre-checks only — the server makes the real decision.
	if (!Rideable)
	{
		UE_LOG(LogDP, Verbose, TEXT("[Rider] RequestEnter ignored: null rideable on %s."), *GetNameSafe(GetOwner()));
		return false;
	}
	if (GetOwnerController() == nullptr)
	{
		UE_LOG(LogDP, Verbose, TEXT("[Rider] RequestEnter ignored: %s has no controller."), *GetNameSafe(GetOwner()));
		return false;
	}
	if (CurrentRideable.IsValid())
	{
		UE_LOG(LogDP, Verbose, TEXT("[Rider] RequestEnter ignored: %s already aboard a ride."), *GetNameSafe(GetOwner()));
		return false;
	}

	if (GetOwner()->HasAuthority())
	{
		// Listen server / standalone: run the authoritative path directly, no RPC.
		return AuthEnter(Rideable, PreferredSeatIndex) == EWS_RideRequestResult::Success;
	}

	Server_Enter(Rideable, PreferredSeatIndex);
	return true;
}

bool UWS_RiderComponent::RequestExit()
{
	if (!CurrentRideable.IsValid())
	{
		return false;
	}

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		return AuthExit() == EWS_RideRequestResult::Success;
	}

	Server_Exit();
	return true;
}

//~ Server RPCs ---------------------------------------------------------------------------------

bool UWS_RiderComponent::Server_Enter_Validate(UWS_RideableComponent* Rideable, int32 PreferredSeatIndex)
{
	// Cheap anti-cheat gate: reject obviously malformed requests before running the handler. The full
	// proximity / free-seat re-derivation happens authoritatively in AuthEnter.
	if (Rideable == nullptr)
	{
		return false;
	}
	// Seat index is either "let the server choose" or a real index into the rideable's seat array.
	if (PreferredSeatIndex != INDEX_NONE &&
		(PreferredSeatIndex < 0 || PreferredSeatIndex >= Rideable->GetSeatCount()))
	{
		return false;
	}
	return true;
}

void UWS_RiderComponent::Server_Enter_Implementation(UWS_RideableComponent* Rideable, int32 PreferredSeatIndex)
{
	AuthEnter(Rideable, PreferredSeatIndex);
}

bool UWS_RiderComponent::Server_Exit_Validate()
{
	return true;
}

void UWS_RiderComponent::Server_Exit_Implementation()
{
	AuthExit();
}

//~ Authoritative mount / dismount --------------------------------------------------------------

EWS_RideRequestResult UWS_RiderComponent::AuthEnter(UWS_RideableComponent* Rideable, int32 PreferredSeatIndex)
{
	// AUTHORITY GUARD AT TOP.
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return EWS_RideRequestResult::InvalidState;
	}
	if (!Rideable)
	{
		return EWS_RideRequestResult::NoRideable;
	}
	if (CurrentRideable.IsValid())
	{
		return EWS_RideRequestResult::AlreadyAboard;
	}

	APawn* Pawn = GetOwnerPawn();
	AController* Controller = GetOwnerController();
	if (!Pawn || !Controller)
	{
		return EWS_RideRequestResult::InvalidState;
	}

	const AActor* RideActor = Rideable->GetOwner();
	if (!RideActor)
	{
		return EWS_RideRequestResult::NoRideable;
	}

	// Re-derive proximity authoritatively (never trust the client's word for it).
	const float MaxDist = ApproxRadius(Pawn) + ApproxRadius(RideActor) + FMath::Max(0.f, MountReachSlack);
	const float DistSq = FVector::DistSquared(Pawn->GetActorLocation(), RideActor->GetActorLocation());
	if (DistSq > FMath::Square(MaxDist))
	{
		UE_LOG(LogDP, Verbose, TEXT("[Rider] %s too far from %s to mount (%.0f > %.0f cm)."),
			*GetNameSafe(Pawn), *GetNameSafe(RideActor), FMath::Sqrt(DistSq), MaxDist);
		return EWS_RideRequestResult::TooFar;
	}

	// Re-derive the seat selection authoritatively: honour a valid free preferred seat, else pick one.
	int32 SeatIndex = PreferredSeatIndex;
	if (SeatIndex == INDEX_NONE || !Rideable->IsSeatFree(SeatIndex))
	{
		SeatIndex = Rideable->FindFreeSeat(/*bPreferDriver*/ true);
	}
	if (SeatIndex == INDEX_NONE)
	{
		return EWS_RideRequestResult::NoFreeSeat;
	}

	// Occupancy bookkeeping (rideable side, authority-guarded internally).
	if (!Rideable->OccupySeat(SeatIndex, Controller))
	{
		return EWS_RideRequestResult::NoFreeSeat;
	}

	// Physical seating + (driver) possession hand-off.
	if (!ApplySeating(Rideable, SeatIndex))
	{
		// Roll back the occupancy if we could not actually seat the pawn.
		Rideable->VacateSeat(SeatIndex);
		return EWS_RideRequestResult::InvalidState;
	}

	// Update the owner-replicated mirror; this OnReps to the owning client and fires its notification.
	CurrentRideable = Rideable;
	CurrentSeatIndex = SeatIndex;

	// Server-local notification (the client fires from OnRep_CurrentRideable).
	OnEnteredRide.Broadcast(Rideable, SeatIndex);

	UE_LOG(LogDP, Log, TEXT("[Rider] %s entered %s seat %d."),
		*GetNameSafe(Pawn), *GetNameSafe(RideActor), SeatIndex);
	return EWS_RideRequestResult::Success;
}

EWS_RideRequestResult UWS_RiderComponent::AuthExit()
{
	// AUTHORITY GUARD AT TOP.
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return EWS_RideRequestResult::InvalidState;
	}

	UWS_RideableComponent* Rideable = CurrentRideable.Get();
	const int32 SeatIndex = CurrentSeatIndex;
	if (!Rideable || SeatIndex == INDEX_NONE)
	{
		// Not aboard; clear any stale mirror defensively.
		CurrentRideable = nullptr;
		CurrentSeatIndex = INDEX_NONE;
		return EWS_RideRequestResult::InvalidState;
	}

	// Reverse seating / possession first, then free the seat.
	ReleaseSeating(Rideable, SeatIndex);
	Rideable->VacateSeat(SeatIndex);

	CurrentRideable = nullptr;
	CurrentSeatIndex = INDEX_NONE;

	OnExitedRide.Broadcast(Rideable);

	UE_LOG(LogDP, Log, TEXT("[Rider] %s exited %s (seat %d)."),
		*GetNameSafe(GetOwnerPawn()), *GetNameSafe(Rideable->GetOwner()), SeatIndex);
	return EWS_RideRequestResult::Success;
}

//~ Seating / possession hand-off (server-only) -------------------------------------------------

bool UWS_RiderComponent::ApplySeating(UWS_RideableComponent* Rideable, int32 SeatIndex)
{
	APawn* Pawn = GetOwnerPawn();
	AController* Controller = GetOwnerController();
	AActor* RideActor = Rideable ? Rideable->GetOwner() : nullptr;
	if (!Pawn || !Controller || !RideActor)
	{
		return false;
	}

	const FName Socket = Rideable->GetSeatSocket(SeatIndex);

	// Attach the rider pawn to the ride at the seat socket. Keep-relative snaps the pawn onto the seat;
	// the engine replicates the attachment to all clients so observers see the rider in the saddle.
	Pawn->AttachToActor(RideActor, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Socket);

	const bool bDriverSeat = Rideable->IsDriverSeat(SeatIndex);
	bDidPossessHandoff = false;
	PossessReturnPawn = nullptr;

	if (bDriverSeat && DriverControlMode == EWS_DriverControlMode::Possess)
	{
		// Possession hand-off: only valid if the rideable actor is itself a Pawn (it owns the movement /
		// input the controller will now drive). If not, degrade to attach-only (documented inert default).
		if (APawn* RidePawn = Cast<APawn>(RideActor))
		{
			PossessReturnPawn = Pawn;            // remember to re-possess on exit
			Controller->UnPossess();             // release the rider pawn
			Controller->Possess(RidePawn);       // take control of the ride via the engine pipeline
			bDidPossessHandoff = true;

			UE_LOG(LogDP, Verbose, TEXT("[Rider] %s possessed ride %s (driver hand-off)."),
				*GetNameSafe(Controller), *GetNameSafe(RidePawn));
		}
		else
		{
			UE_LOG(LogDP, Warning, TEXT("[Rider] Driver hand-off requested but ride %s is not a Pawn; "
				"degrading to attach-only. The rider stays possessed and input forwarding (if any) is the "
				"ride's responsibility."), *GetNameSafe(RideActor));
		}
	}
	// Passenger seats and AttachOnly drivers: no possession change. The pawn stays possessed by its own
	// controller and simply rides along attached to the socket; any input routing to the ride is the
	// ride's concern (this component does not forge a second input chain).

	return true;
}

void UWS_RiderComponent::ReleaseSeating(UWS_RideableComponent* Rideable, int32 SeatIndex)
{
	APawn* Pawn = GetOwnerPawn();
	AController* Controller = GetOwnerController();

	if (bDidPossessHandoff)
	{
		// Reverse the possession hand-off: re-possess the original rider pawn. Note the controller in
		// scope here is the ride's controller (it currently possesses the ride), which is the same
		// AController object we used at enter time — GetOwnerController() resolves via the rider pawn, so
		// recover the controller from the ride instead.
		AActor* RideActor = Rideable ? Rideable->GetOwner() : nullptr;
		APawn* RidePawn = Cast<APawn>(RideActor);
		AController* RideController = RidePawn ? RidePawn->GetController() : Controller;

		APawn* ReturnPawn = PossessReturnPawn.Get();
		if (RideController)
		{
			RideController->UnPossess();
			if (ReturnPawn)
			{
				RideController->Possess(ReturnPawn);
			}
		}
		bDidPossessHandoff = false;
		PossessReturnPawn = nullptr;

		// Re-resolve the rider pawn after re-possession for the detach below.
		Pawn = ReturnPawn ? ReturnPawn : GetOwnerPawn();
	}

	// Detach the rider pawn back into the world, keeping its current world transform.
	if (!Pawn)
	{
		Pawn = GetOwnerPawn();
	}
	if (Pawn)
	{
		Pawn->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	}
}

//~ OnRep ---------------------------------------------------------------------------------------

void UWS_RiderComponent::OnRep_CurrentRideable(const TWeakObjectPtr<UWS_RideableComponent>& OldRideable)
{
	// Fire the matching local notification on the owning client so UI/camera react. The authoritative
	// attach/possession is replicated by the engine independently; this is purely the gameplay event.
	if (CurrentRideable.IsValid())
	{
		OnEnteredRide.Broadcast(CurrentRideable.Get(), CurrentSeatIndex);
	}
	else if (OldRideable.IsValid())
	{
		OnExitedRide.Broadcast(OldRideable.Get());
	}
}
