// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Jobs/SimAg_JobReservationSubsystem.h"
#include "Jobs/SimAg_ReservationReplicator.h"
#include "Clock/SimAg_ClockSubsystem.h"
#include "DesignPatternsSimAgentsTags.h"
#include "Settings/SimAg_DeveloperSettings.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"
#include "EngineUtils.h"
#include "Engine/World.h"

#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

//~ Lifecycle -------------------------------------------------------------------------------------

void USimAg_JobReservationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (const USimAg_DeveloperSettings* Settings = USimAg_DeveloperSettings::Get())
	{
		ExpiryDays = FMath::Max(1e-3f, Settings->ReservationExpiryDays);
	}
	RegisteredServiceTag = SimAgNativeTags::Service_JobReservation;

	// Clients may already have a replicated carrier; cache it if present.
	ResolveCarrier();

	RegisterAsReservationProvider();

	UE_LOG(LogDP, Log, TEXT("SimAg reservation router initialized (authority=%d, expiryDays=%.3f)."),
		HasWorldAuthority() ? 1 : 0, ExpiryDays);
}

void USimAg_JobReservationSubsystem::Deinitialize()
{
	if (RegisteredServiceTag.IsValid())
	{
		if (UDP_ServiceLocatorSubsystem* Locator =
			FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
		{
			Locator->UnregisterService(RegisteredServiceTag);
		}
	}
	Carrier.Reset();
	Super::Deinitialize();
}

void USimAg_JobReservationSubsystem::RegisterAsReservationProvider()
{
	if (!RegisteredServiceTag.IsValid())
	{
		return;
	}
	if (UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		Locator->RegisterService(RegisteredServiceTag, this, EDP_ServiceLifetime::WeakObserved, /*bAllowOverride*/ true);
	}
}

//~ Carrier resolution ----------------------------------------------------------------------------

ASimAg_ReservationReplicator* USimAg_JobReservationSubsystem::ResolveCarrier() const
{
	if (ASimAg_ReservationReplicator* Live = Carrier.Get())
	{
		return Live;
	}
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<ASimAg_ReservationReplicator> It(World); It; ++It)
		{
			if (ASimAg_ReservationReplicator* Found = *It)
			{
				const_cast<USimAg_JobReservationSubsystem*>(this)->Carrier = Found;
				return Found;
			}
		}
	}
	return nullptr;
}

ASimAg_ReservationReplicator* USimAg_JobReservationSubsystem::GetOrSpawnCarrier(bool bSpawnIfMissing)
{
	if (ASimAg_ReservationReplicator* Live = ResolveCarrier())
	{
		return Live;
	}
	if (!bSpawnIfMissing || !HasWorldAuthority())
	{
		return nullptr;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;

	ASimAg_ReservationReplicator* Spawned = World->SpawnActor<ASimAg_ReservationReplicator>(
		ASimAg_ReservationReplicator::StaticClass(), FTransform::Identity, Params);
	if (!Spawned)
	{
		UE_LOG(LogDP, Error, TEXT("SimAg: failed to spawn reservation carrier."));
		return nullptr;
	}
	Carrier = Spawned;
	return Spawned;
}

//~ ISeam_JobReservation --------------------------------------------------------------------------

bool USimAg_JobReservationSubsystem::TryReserve_Implementation(FSeam_EntityId Target, FSeam_EntityId Agent)
{
	// AUTHORITY GUARD at top.
	if (!HasWorldAuthority())
	{
		return false;
	}
	ASimAg_ReservationReplicator* Live = GetOrSpawnCarrier(/*bSpawnIfMissing*/ true);
	if (!Live)
	{
		return false;
	}
	const double NowDays = GetNowDays();
	Live->PruneExpired(NowDays);
	return Live->Reserve(Target, Agent, NowDays + static_cast<double>(ExpiryDays), NowDays);
}

void USimAg_JobReservationSubsystem::Release_Implementation(FSeam_EntityId Target)
{
	if (!HasWorldAuthority())
	{
		return;
	}
	if (ASimAg_ReservationReplicator* Live = GetOrSpawnCarrier(/*bSpawnIfMissing*/ false))
	{
		Live->Release(Target);
	}
}

bool USimAg_JobReservationSubsystem::IsReserved_Implementation(FSeam_EntityId Target) const
{
	const ASimAg_ReservationReplicator* Live = ResolveCarrier();
	if (!Live)
	{
		return false;
	}
	if (const FSimAg_Reservation* Found = Live->Find(Target))
	{
		return Found->IsLiveAt(GetNowDays());
	}
	return false;
}

//~ Reads -----------------------------------------------------------------------------------------

FSeam_EntityId USimAg_JobReservationSubsystem::GetReservationHolder(FSeam_EntityId Target) const
{
	const ASimAg_ReservationReplicator* Live = ResolveCarrier();
	if (Live)
	{
		if (const FSimAg_Reservation* Found = Live->Find(Target))
		{
			if (Found->IsLiveAt(GetNowDays()))
			{
				return Found->Agent;
			}
		}
	}
	return FSeam_EntityId::Invalid();
}

int32 USimAg_JobReservationSubsystem::GetReservationCount() const
{
	const ASimAg_ReservationReplicator* Live = ResolveCarrier();
	if (!Live)
	{
		return 0;
	}
	const double NowDays = GetNowDays();
	int32 Count = 0;
	for (const FSimAg_Reservation& R : Live->GetReservations())
	{
		if (R.IsLiveAt(NowDays))
		{
			++Count;
		}
	}
	return Count;
}

//~ ISeam_Persistable -----------------------------------------------------------------------------

void USimAg_JobReservationSubsystem::CaptureState_Implementation(FInstancedStruct& Out) const
{
	FSimAg_ReservationRecord Record;
	if (HasWorldAuthority())
	{
		if (const ASimAg_ReservationReplicator* Live = ResolveCarrier())
		{
			const double NowDays = GetNowDays();
			for (const FSimAg_Reservation& R : Live->GetReservations())
			{
				if (!R.IsLiveAt(NowDays))
				{
					continue; // don't persist expired reservations
				}
				FSimAg_SavedReservation Saved;
				Saved.Target = R.Target;
				Saved.Agent = R.Agent;
				Saved.ExpiryDays = R.ExpiryDays;
				Record.Reservations.Add(Saved);
			}
		}
	}
	Out = FInstancedStruct::Make(Record);
}

void USimAg_JobReservationSubsystem::RestoreState_Implementation(const FInstancedStruct& In)
{
	// AUTHORITY GUARD: a client load is a no-op.
	if (!HasWorldAuthority())
	{
		return;
	}
	const FSimAg_ReservationRecord* Record = In.GetPtr<FSimAg_ReservationRecord>();
	if (!Record)
	{
		UE_LOG(LogDP, Warning, TEXT("SimAg reservation RestoreState: record was not FSimAg_ReservationRecord."));
		return;
	}

	ASimAg_ReservationReplicator* Live = GetOrSpawnCarrier(/*bSpawnIfMissing*/ true);
	if (!Live)
	{
		return;
	}
	Live->ClearAll();
	for (const FSimAg_SavedReservation& Saved : Record->Reservations)
	{
		FSimAg_Reservation Reservation(Saved.Target, Saved.Agent, Saved.ExpiryDays);
		Live->RestoreReservation(Reservation);
	}
	UE_LOG(LogDP, Log, TEXT("SimAg reservation restored %d reservations."), Record->Reservations.Num());
}

FGameplayTag USimAg_JobReservationSubsystem::GetPersistenceKind_Implementation() const
{
	return SimAgNativeTags::Persist_Reservation;
}

//~ Internals -------------------------------------------------------------------------------------

USimAg_ClockSubsystem* USimAg_JobReservationSubsystem::GetClock() const
{
	if (CachedClock.IsValid())
	{
		return CachedClock.Get();
	}
	USimAg_JobReservationSubsystem* MutableThis = const_cast<USimAg_JobReservationSubsystem*>(this);

	UObject* ClockObj = nullptr;
	if (UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		ClockObj = Locator->ResolveService(SimAgNativeTags::Service_Clock);
	}
	USimAg_ClockSubsystem* Clock = Cast<USimAg_ClockSubsystem>(ClockObj);
	if (!Clock)
	{
		Clock = FDP_SubsystemStatics::GetWorldSubsystem<USimAg_ClockSubsystem>(this);
	}
	MutableThis->CachedClock = Clock;
	return Clock;
}

double USimAg_JobReservationSubsystem::GetNowDays() const
{
	if (USimAg_ClockSubsystem* Clock = GetClock())
	{
		return static_cast<double>(Clock->GetDayNumber_Implementation())
			+ static_cast<double>(Clock->GetNormalizedTimeOfDay_Implementation());
	}
	return 0.0;
}

//~ Debug -----------------------------------------------------------------------------------------

FString USimAg_JobReservationSubsystem::GetDPDebugString_Implementation() const
{
	return FString::Printf(TEXT("SimAg Reservations: %s | live=%d"),
		HasWorldAuthority() ? TEXT("AUTHORITY") : TEXT("client"), GetReservationCount());
}
