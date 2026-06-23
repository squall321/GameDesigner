// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Memory/SimAg_MemoryComponent.h"
#include "Brain/SimAg_AgentComponent.h"
#include "Clock/SimAg_ClockSubsystem.h"
#include "Settings/SimAg_DeveloperSettings.h"
#include "DesignPatternsSimAgentsTags.h"
#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/Actor.h"

#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

//~ FSimAg_MemoryFact fast-array callbacks (clients only) ------------------------------------------

void FSimAg_MemoryFact::PostReplicatedAdd(const FSimAg_MemoryArray& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->HandleReplicatedChange();
	}
}

void FSimAg_MemoryFact::PostReplicatedChange(const FSimAg_MemoryArray& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->HandleReplicatedChange();
	}
}

void FSimAg_MemoryFact::PreReplicatedRemove(const FSimAg_MemoryArray& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->HandleReplicatedChange();
	}
}

//~ USimAg_MemoryComponent ------------------------------------------------------------------------

USimAg_MemoryComponent::USimAg_MemoryComponent()
{
	// Aging runs per server frame; ticking is enabled but the tick body early-returns on clients.
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);

	Memory.OwnerComponent = this;
}

void USimAg_MemoryComponent::BeginPlay()
{
	Super::BeginPlay();

	if (const USimAg_DeveloperSettings* Settings = USimAg_DeveloperSettings::Get())
	{
		ReplicationCadence = FMath::Max(0.05f, Settings->MemoryReplicationCadence);
		HalfLifeDays = FMath::Max(1e-4f, Settings->MemoryHalfLifeDays);
		PruneConfidence = FMath::Clamp(Settings->MemoryPruneConfidence, 0.f, 1.f);
	}

	// Re-assert the back-pointer (BeginPlay runs on both server and clients).
	Memory.OwnerComponent = this;

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		const double NowDays = GetNowDays();
		Memory.Facts.Reset();
		for (const FSimAg_MemoryFact& Default : DefaultFacts)
		{
			if (!Default.Subject.IsValid())
			{
				continue;
			}
			FSimAg_MemoryFact Fact = Default;
			Fact.Confidence = FMath::Clamp(Fact.Confidence, 0.f, 1.f);
			// Stamp an unset time to "now" so authored facts decay from the start of play, not day 0.
			if (Fact.LastSeenDays <= 0.0)
			{
				Fact.LastSeenDays = NowDays;
			}
			Memory.Facts.Add(Fact);
		}
		Memory.MarkArrayDirty();
	}
}

void USimAg_MemoryComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Only authority ages / prunes / replicates; clients purely observe.
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	// Pruning uses the live decayed confidence (which already factors in elapsed sim days), so it is the
	// single place "aging" is realized — there is no per-frame Confidence mutation, keeping the stored
	// value stable and the wire quiet between prunes.
	PruneFaded();

	ReplicationAccumulator += DeltaTime;
	if (ReplicationAccumulator >= ReplicationCadence)
	{
		ReplicationAccumulator = 0.f;
		// A prune already marks the array dirty; nothing else to flush on cadence by itself.
	}
}

void USimAg_MemoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(USimAg_MemoryComponent, Memory);
}

//~ Mutators (authority only) ---------------------------------------------------------------------

void USimAg_MemoryComponent::RememberFact(const FSimAg_MemoryFact& InFact)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	if (!InFact.Subject.IsValid())
	{
		return;
	}

	const double NowDays = GetNowDays();

	if (FSimAg_MemoryFact* Existing = FindFact(InFact.Subject, InFact.Entity))
	{
		Existing->WorldLocation = InFact.WorldLocation;
		Existing->Confidence = FMath::Clamp(InFact.Confidence, 0.f, 1.f);
		Existing->LastSeenDays = (InFact.LastSeenDays > 0.0) ? InFact.LastSeenDays : NowDays;
		Memory.MarkItemDirty(*Existing);
	}
	else
	{
		FSimAg_MemoryFact Fact = InFact;
		Fact.Confidence = FMath::Clamp(Fact.Confidence, 0.f, 1.f);
		if (Fact.LastSeenDays <= 0.0)
		{
			Fact.LastSeenDays = NowDays;
		}
		FSimAg_MemoryFact& Added = Memory.Facts.Add_GetRef(Fact);
		Memory.MarkItemDirty(Added);
	}

	OnMemoryChanged.Broadcast(this);
}

int32 USimAg_MemoryComponent::ForgetSubject(FGameplayTag Subject)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return 0;
	}
	const int32 Removed = Memory.Facts.RemoveAll([&Subject](const FSimAg_MemoryFact& F)
	{
		return F.Subject == Subject;
	});
	if (Removed > 0)
	{
		Memory.MarkArrayDirty();
		OnMemoryChanged.Broadcast(this);
	}
	return Removed;
}

//~ Reads (client-safe) ---------------------------------------------------------------------------

bool USimAg_MemoryComponent::QueryNearest(FGameplayTag Subject, const FVector& From, FSimAg_MemoryFact& Out) const
{
	const double NowDays = GetNowDays();
	const FSimAg_MemoryFact* Best = nullptr;
	double BestDistSq = TNumericLimits<double>::Max();

	for (const FSimAg_MemoryFact& Fact : Memory.Facts)
	{
		if (!Fact.Subject.MatchesTag(Subject))
		{
			continue;
		}
		if (Fact.GetDecayedConfidence(NowDays, HalfLifeDays) < PruneConfidence)
		{
			continue;
		}
		const double DistSq = FVector::DistSquared(Fact.WorldLocation, From);
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = &Fact;
		}
	}

	if (Best)
	{
		Out = *Best;
		return true;
	}
	return false;
}

bool USimAg_MemoryComponent::QueryStrongest(FGameplayTag Subject, FSimAg_MemoryFact& Out) const
{
	const double NowDays = GetNowDays();
	const FSimAg_MemoryFact* Best = nullptr;
	float BestConf = PruneConfidence;

	for (const FSimAg_MemoryFact& Fact : Memory.Facts)
	{
		if (!Fact.Subject.MatchesTag(Subject))
		{
			continue;
		}
		const float Conf = Fact.GetDecayedConfidence(NowDays, HalfLifeDays);
		if (Conf >= BestConf)
		{
			BestConf = Conf;
			Best = &Fact;
		}
	}

	if (Best)
	{
		Out = *Best;
		return true;
	}
	return false;
}

float USimAg_MemoryComponent::GetDecayedConfidenceFor(FGameplayTag Subject) const
{
	FSimAg_MemoryFact Fact;
	if (QueryStrongest(Subject, Fact))
	{
		return Fact.GetDecayedConfidence(GetNowDays(), HalfLifeDays);
	}
	return 0.f;
}

void USimAg_MemoryComponent::HandleReplicatedChange()
{
	OnMemoryChanged.Broadcast(this);
}

//~ ISeam_Persistable -----------------------------------------------------------------------------

void USimAg_MemoryComponent::CaptureState_Implementation(FInstancedStruct& Out) const
{
	FSimAg_MemoryRecord Record;
	Record.AgentId = ResolveAgentId();
	Record.Facts.Reserve(Memory.Facts.Num());
	for (const FSimAg_MemoryFact& Fact : Memory.Facts)
	{
		FSimAg_SavedMemoryFact Saved;
		Saved.Subject = Fact.Subject;
		Saved.WorldLocation = Fact.WorldLocation;
		Saved.Entity = Fact.Entity;
		Saved.Confidence = Fact.Confidence;
		Saved.LastSeenDays = Fact.LastSeenDays;
		Record.Facts.Add(Saved);
	}
	Out.InitializeAs<FSimAg_MemoryRecord>(Record);
}

void USimAg_MemoryComponent::RestoreState_Implementation(const FInstancedStruct& In)
{
	// Authority guard: a client-side load is a no-op (it lacks authoritative state).
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	const FSimAg_MemoryRecord* Record = In.GetPtr<FSimAg_MemoryRecord>();
	if (!Record)
	{
		return;
	}

	Memory.Facts.Reset();
	for (const FSimAg_SavedMemoryFact& Saved : Record->Facts)
	{
		if (!Saved.Subject.IsValid())
		{
			continue;
		}
		FSimAg_MemoryFact Fact;
		Fact.Subject = Saved.Subject;
		Fact.WorldLocation = Saved.WorldLocation;
		Fact.Entity = Saved.Entity;
		Fact.Confidence = FMath::Clamp(Saved.Confidence, 0.f, 1.f);
		Fact.LastSeenDays = Saved.LastSeenDays;
		Memory.Facts.Add(Fact);
	}
	Memory.MarkArrayDirty();
	OnMemoryChanged.Broadcast(this);
}

FGameplayTag USimAg_MemoryComponent::GetPersistenceKind_Implementation() const
{
	return SimAgNativeTags::Persist_Memory;
}

//~ Internals -------------------------------------------------------------------------------------

double USimAg_MemoryComponent::GetNowDays() const
{
	if (USimAg_ClockSubsystem* Clock = GetClock())
	{
		// Day number + time-of-day = fractional days (matches the clock's internal source of truth).
		return static_cast<double>(Clock->GetDayNumber_Implementation())
			+ static_cast<double>(Clock->GetNormalizedTimeOfDay_Implementation());
	}
	return 0.0;
}

USimAg_ClockSubsystem* USimAg_MemoryComponent::GetClock() const
{
	if (CachedClock.IsValid())
	{
		return CachedClock.Get();
	}
	USimAg_MemoryComponent* MutableThis = const_cast<USimAg_MemoryComponent*>(this);

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

void USimAg_MemoryComponent::PruneFaded()
{
	const double NowDays = GetNowDays();
	const int32 Removed = Memory.Facts.RemoveAll([this, NowDays](const FSimAg_MemoryFact& F)
	{
		return F.GetDecayedConfidence(NowDays, HalfLifeDays) < PruneConfidence;
	});
	if (Removed > 0)
	{
		Memory.MarkArrayDirty();
		OnMemoryChanged.Broadcast(this);
	}
}

FSimAg_MemoryFact* USimAg_MemoryComponent::FindFact(const FGameplayTag& Subject, const FSeam_EntityId& Entity)
{
	return Memory.Facts.FindByPredicate([&Subject, &Entity](const FSimAg_MemoryFact& F)
	{
		return F.Subject == Subject && F.Entity == Entity;
	});
}

FSeam_EntityId USimAg_MemoryComponent::ResolveAgentId() const
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
