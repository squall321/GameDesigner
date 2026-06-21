// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Ride/WS_RideableComponent.h"
#include "Identity/Seam_EntityIdentity.h"
#include "Core/DPLog.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"

//~ FWS_SeatOccupancy fast-array callbacks (client side) ----------------------------------------

void FWS_SeatOccupancy::PreReplicatedRemove(const FWS_SeatOccupancyArray& InArraySerializer)
{
	// A removed row means the seat just became free on the client mirror.
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->HandleReplicatedOccupancyChange(SeatIndex, /*bNowOccupied*/ false);
	}
}

void FWS_SeatOccupancy::PostReplicatedAdd(const FWS_SeatOccupancyArray& InArraySerializer)
{
	// A new row means the seat just became occupied on the client mirror.
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->HandleReplicatedOccupancyChange(SeatIndex, /*bNowOccupied*/ true);
	}
}

void FWS_SeatOccupancy::PostReplicatedChange(const FWS_SeatOccupancyArray& InArraySerializer)
{
	// The occupant of an already-occupied seat changed (e.g. controller hand-off). Still "occupied".
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->HandleReplicatedOccupancyChange(SeatIndex, /*bNowOccupied*/ true);
	}
}

//~ UWS_RideableComponent -----------------------------------------------------------------------

UWS_RideableComponent::UWS_RideableComponent()
{
	// Carrier component: occupancy is replicated authoritative state, so replicate by default.
	SetIsReplicatedByDefault(true);
	bWantsInitializeComponent = true;

	// Occupancy only changes on mount/dismount; no per-frame work.
	PrimaryComponentTick.bCanEverTick = false;

	// Wire the fast-array back-pointer so per-item callbacks can notify us (server and client).
	Occupancy.OwnerComponent = this;
}

void UWS_RideableComponent::InitializeComponent()
{
	Super::InitializeComponent();
	// Re-assert the back-pointer after any sub-object fixup / net construction.
	Occupancy.OwnerComponent = this;
}

void UWS_RideableComponent::BeginPlay()
{
	Super::BeginPlay();
	Occupancy.OwnerComponent = this;

	// Validate designer setup once so misconfiguration is visible in logs rather than silent.
	if (Seats.Num() == 0)
	{
		UE_LOG(LogDP, Warning, TEXT("[Ride] %s has no seats authored; ride is uninhabitable."),
			*GetNameSafe(GetOwner()));
	}
}

void UWS_RideableComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// On authority, free every seat so any rider components / possession state are not left dangling.
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		for (int32 i = Occupancy.Rows.Num() - 1; i >= 0; --i)
		{
			VacateSeat(Occupancy.Rows[i].SeatIndex);
		}
	}
	Super::EndPlay(EndPlayReason);
}

void UWS_RideableComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UWS_RideableComponent, Occupancy);
}

//~ Read-side queries ---------------------------------------------------------------------------

const FWS_SeatOccupancy* UWS_RideableComponent::FindRow(int32 SeatIndex) const
{
	return Occupancy.Rows.FindByPredicate(
		[SeatIndex](const FWS_SeatOccupancy& Row) { return Row.SeatIndex == SeatIndex; });
}

FWS_SeatOccupancy* UWS_RideableComponent::FindRowMutable(int32 SeatIndex)
{
	return Occupancy.Rows.FindByPredicate(
		[SeatIndex](const FWS_SeatOccupancy& Row) { return Row.SeatIndex == SeatIndex; });
}

bool UWS_RideableComponent::IsSeatFree(int32 SeatIndex) const
{
	return Seats.IsValidIndex(SeatIndex) && FindRow(SeatIndex) == nullptr;
}

bool UWS_RideableComponent::IsDriverSeat(int32 SeatIndex) const
{
	return Seats.IsValidIndex(SeatIndex) && Seats[SeatIndex].bDriver;
}

int32 UWS_RideableComponent::GetDriverSeatIndex() const
{
	for (int32 i = 0; i < Seats.Num(); ++i)
	{
		if (Seats[i].bDriver)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

int32 UWS_RideableComponent::FindFreeSeat(bool bPreferDriver) const
{
	// First pass: honour the driver preference so a lone rider mounts as driver when possible.
	if (bPreferDriver)
	{
		const int32 DriverIdx = GetDriverSeatIndex();
		if (DriverIdx != INDEX_NONE && IsSeatFree(DriverIdx))
		{
			return DriverIdx;
		}
	}

	// Second pass: first free seat of any kind.
	for (int32 i = 0; i < Seats.Num(); ++i)
	{
		if (IsSeatFree(i))
		{
			return i;
		}
	}
	return INDEX_NONE;
}

FName UWS_RideableComponent::GetSeatSocket(int32 SeatIndex) const
{
	return Seats.IsValidIndex(SeatIndex) ? Seats[SeatIndex].Socket : NAME_None;
}

AController* UWS_RideableComponent::GetSeatOccupantController(int32 SeatIndex) const
{
	const FWS_SeatOccupancy* Row = FindRow(SeatIndex);
	return (Row && Row->OccupantController.IsValid()) ? Row->OccupantController.Get() : nullptr;
}

int32 UWS_RideableComponent::FindSeatOfController(const AController* Controller) const
{
	if (!Controller)
	{
		return INDEX_NONE;
	}
	for (const FWS_SeatOccupancy& Row : Occupancy.Rows)
	{
		if (Row.OccupantController.Get() == Controller)
		{
			return Row.SeatIndex;
		}
	}
	return INDEX_NONE;
}

bool UWS_RideableComponent::IsControllerAboard(const AController* Controller) const
{
	return FindSeatOfController(Controller) != INDEX_NONE;
}

//~ Authority-only mutation ---------------------------------------------------------------------

bool UWS_RideableComponent::OccupySeat(int32 SeatIndex, AController* Occupant)
{
	// AUTHORITY GUARD AT TOP: clients never mutate occupancy; they receive it via delta-replication.
	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return false;
	}

	if (!Seats.IsValidIndex(SeatIndex))
	{
		UE_LOG(LogDP, Warning, TEXT("[Ride] OccupySeat rejected: bad seat index %d on %s (seats=%d)."),
			SeatIndex, *GetNameSafe(Owner), Seats.Num());
		return false;
	}

	if (FindRow(SeatIndex) != nullptr)
	{
		// Seat already taken — caller (rider server RPC) should have validated, but stay defensive.
		UE_LOG(LogDP, Verbose, TEXT("[Ride] OccupySeat rejected: seat %d already occupied on %s."),
			SeatIndex, *GetNameSafe(Owner));
		return false;
	}

	FWS_SeatOccupancy NewRow(SeatIndex);
	NewRow.OccupantController = Occupant;

	// Record a stable id if the occupant's pawn exposes the identity seam (preferred resolution on
	// clients that may not have the controller relevant). Resolved off the actor via Implements<>.
	if (Occupant)
	{
		const APawn* Pawn = Occupant->GetPawn();
		const UObject* IdentitySource = nullptr;
		if (Pawn && Pawn->Implements<USeam_EntityIdentity>())
		{
			IdentitySource = Pawn;
		}
		else if (Occupant->Implements<USeam_EntityIdentity>())
		{
			IdentitySource = Occupant;
		}
		if (IdentitySource)
		{
			NewRow.OccupantId = ISeam_EntityIdentity::Execute_GetEntityId(IdentitySource);
		}
	}

	FWS_SeatOccupancy& Added = Occupancy.Rows[Occupancy.Rows.Add(NewRow)];
	Occupancy.MarkItemDirty(Added);

	// Authority refires locally too (clients fire from the fast-array callback).
	OnSeatOccupancyChanged.Broadcast(SeatIndex, /*bNowOccupied*/ true);

	UE_LOG(LogDP, Log, TEXT("[Ride] Seat %d occupied by %s on %s (driver=%d)."),
		SeatIndex, *GetNameSafe(Occupant), *GetNameSafe(Owner), IsDriverSeat(SeatIndex) ? 1 : 0);
	return true;
}

bool UWS_RideableComponent::VacateSeat(int32 SeatIndex)
{
	// AUTHORITY GUARD AT TOP.
	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return false;
	}

	const int32 RowIndex = Occupancy.Rows.IndexOfByPredicate(
		[SeatIndex](const FWS_SeatOccupancy& Row) { return Row.SeatIndex == SeatIndex; });
	if (RowIndex == INDEX_NONE)
	{
		// Idempotent: already free.
		return false;
	}

	Occupancy.Rows.RemoveAt(RowIndex);
	Occupancy.MarkArrayDirty();

	OnSeatOccupancyChanged.Broadcast(SeatIndex, /*bNowOccupied*/ false);

	UE_LOG(LogDP, Log, TEXT("[Ride] Seat %d vacated on %s."), SeatIndex, *GetNameSafe(Owner));
	return true;
}

bool UWS_RideableComponent::VacateController(AController* Controller)
{
	const int32 SeatIndex = FindSeatOfController(Controller);
	return SeatIndex != INDEX_NONE && VacateSeat(SeatIndex);
}

//~ Notifications -------------------------------------------------------------------------------

void UWS_RideableComponent::HandleReplicatedOccupancyChange(int32 SeatIndex, bool bNowOccupied)
{
	// Client-side bridge from the fast-array item callbacks to the shared delegate.
	OnSeatOccupancyChanged.Broadcast(SeatIndex, bNowOccupied);
}
