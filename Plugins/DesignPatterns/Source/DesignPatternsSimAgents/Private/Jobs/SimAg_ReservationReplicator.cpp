// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Jobs/SimAg_ReservationReplicator.h"
#include "Core/DPLog.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"

//~ FSimAg_Reservation fast-array callbacks (clients only) ----------------------------------------

void FSimAg_Reservation::PostReplicatedAdd(const FSimAg_ReservationArray& InArraySerializer)
{
	if (InArraySerializer.OwnerCarrier)
	{
		InArraySerializer.OwnerCarrier->HandleReplicatedChange(Target);
	}
}

void FSimAg_Reservation::PostReplicatedChange(const FSimAg_ReservationArray& InArraySerializer)
{
	if (InArraySerializer.OwnerCarrier)
	{
		InArraySerializer.OwnerCarrier->HandleReplicatedChange(Target);
	}
}

void FSimAg_Reservation::PreReplicatedRemove(const FSimAg_ReservationArray& InArraySerializer)
{
	if (InArraySerializer.OwnerCarrier)
	{
		InArraySerializer.OwnerCarrier->HandleReplicatedChange(Target);
	}
}

//~ ASimAg_ReservationReplicator ------------------------------------------------------------------

ASimAg_ReservationReplicator::ASimAg_ReservationReplicator()
{
	bReplicates = true;
	bAlwaysRelevant = true; // a single world reservation set is relevant to every connection
	NetDormancy = DORM_Initial;
	SetReplicatingMovement(false);
	PrimaryActorTick.bCanEverTick = false;

	Reservations.OwnerCarrier = this;
}

void ASimAg_ReservationReplicator::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	Reservations.OwnerCarrier = this;
}

void ASimAg_ReservationReplicator::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASimAg_ReservationReplicator, Reservations);
}

void ASimAg_ReservationReplicator::WakeForChange()
{
	if (NetDormancy > DORM_Awake)
	{
		FlushNetDormancy();
	}
}

//~ Reads -----------------------------------------------------------------------------------------

const FSimAg_Reservation* ASimAg_ReservationReplicator::Find(const FSeam_EntityId& Target) const
{
	return Reservations.Reservations.FindByPredicate([&Target](const FSimAg_Reservation& R) { return R.Target == Target; });
}

FSimAg_Reservation* ASimAg_ReservationReplicator::FindMutable(const FSeam_EntityId& Target)
{
	return Reservations.Reservations.FindByPredicate([&Target](const FSimAg_Reservation& R) { return R.Target == Target; });
}

//~ Authority mutators ----------------------------------------------------------------------------

bool ASimAg_ReservationReplicator::Reserve(const FSeam_EntityId& Target, const FSeam_EntityId& Agent, double ExpiryDays, double NowDays)
{
	// AUTHORITY GUARD at top.
	if (!HasAuthority())
	{
		return false;
	}
	if (!Target.IsValid() || !Agent.IsValid())
	{
		return false;
	}

	if (FSimAg_Reservation* Existing = FindMutable(Target))
	{
		// Free if expired or already ours: refresh the holder/expiry. Otherwise the target is taken.
		if (Existing->IsLiveAt(NowDays) && Existing->Agent != Agent)
		{
			return false;
		}
		Existing->Agent = Agent;
		Existing->ExpiryDays = ExpiryDays;
		Reservations.MarkItemDirty(*Existing);
	}
	else
	{
		FSimAg_Reservation& Added = Reservations.Reservations.Add_GetRef(FSimAg_Reservation(Target, Agent, ExpiryDays));
		Reservations.MarkItemDirty(Added);
	}

	WakeForChange();
	OnReservationChanged.Broadcast(this, Target);
	return true;
}

bool ASimAg_ReservationReplicator::Release(const FSeam_EntityId& Target)
{
	if (!HasAuthority())
	{
		return false;
	}
	const int32 Removed = Reservations.Reservations.RemoveAll([&Target](const FSimAg_Reservation& R) { return R.Target == Target; });
	if (Removed > 0)
	{
		Reservations.MarkArrayDirty();
		WakeForChange();
		OnReservationChanged.Broadcast(this, Target);
		return true;
	}
	return false;
}

int32 ASimAg_ReservationReplicator::PruneExpired(double NowDays)
{
	if (!HasAuthority())
	{
		return 0;
	}
	const int32 Removed = Reservations.Reservations.RemoveAll([NowDays](const FSimAg_Reservation& R)
	{
		return !R.IsLiveAt(NowDays);
	});
	if (Removed > 0)
	{
		Reservations.MarkArrayDirty();
		WakeForChange();
	}
	return Removed;
}

void ASimAg_ReservationReplicator::ClearAll()
{
	if (!HasAuthority())
	{
		return;
	}
	if (Reservations.Reservations.Num() > 0)
	{
		Reservations.Reservations.Reset();
		Reservations.MarkArrayDirty();
		WakeForChange();
	}
}

void ASimAg_ReservationReplicator::RestoreReservation(const FSimAg_Reservation& Reservation)
{
	if (!HasAuthority())
	{
		return;
	}
	if (!Reservation.Target.IsValid())
	{
		return;
	}
	if (FSimAg_Reservation* Existing = FindMutable(Reservation.Target))
	{
		*Existing = Reservation;
		Reservations.MarkItemDirty(*Existing);
	}
	else
	{
		FSimAg_Reservation& Added = Reservations.Reservations.Add_GetRef(Reservation);
		Reservations.MarkItemDirty(Added);
	}
	WakeForChange();
}

//~ Client-side change surfacing ------------------------------------------------------------------

void ASimAg_ReservationReplicator::HandleReplicatedChange(const FSeam_EntityId& Target)
{
	OnReservationChanged.Broadcast(this, Target);
}
