// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Needs/SimAg_NeedsComponent.h"
#include "Clock/SimAg_ClockSubsystem.h"
#include "Settings/SimAg_DeveloperSettings.h"
#include "DesignPatternsSimAgentsTags.h"
#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Clock/Seam_SimClock.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/Actor.h"

#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

//~ FSimAg_NeedMeter fast-array callbacks (clients only) -------------------------------------------

void FSimAg_NeedMeter::PostReplicatedAdd(const FSimAg_NeedsArray& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->HandleReplicatedChange();
	}
}

void FSimAg_NeedMeter::PostReplicatedChange(const FSimAg_NeedsArray& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->HandleReplicatedChange();
	}
}

void FSimAg_NeedMeter::PreReplicatedRemove(const FSimAg_NeedsArray& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->HandleReplicatedChange();
	}
}

//~ USimAg_NeedsComponent --------------------------------------------------------------------------

USimAg_NeedsComponent::USimAg_NeedsComponent()
{
	// Drains run per server frame; ticking is enabled but the tick body early-returns on clients.
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);

	// Wire the back-pointer so client-side fast-array callbacks can notify us.
	Needs.OwnerComponent = this;
}

void USimAg_NeedsComponent::BeginPlay()
{
	Super::BeginPlay();

	// Cache the replication cadence from settings (clamped to a sane minimum to avoid div/zero spam).
	if (const USimAg_DeveloperSettings* Settings = USimAg_DeveloperSettings::Get())
	{
		ReplicationCadence = FMath::Max(0.05f, Settings->NeedsReplicationCadence);
	}

	// Re-assert the back-pointer (BeginPlay runs on both server and clients).
	Needs.OwnerComponent = this;

	// Authority seeds the replicated meter set from the authored defaults.
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		Needs.Meters.Reset();
		for (const FSimAg_NeedMeter& Default : DefaultNeeds)
		{
			if (Default.Need.IsValid())
			{
				FSimAg_NeedMeter Meter = Default;
				Meter.Current = FMath::Clamp(Meter.Current, 0.f, Meter.Max);
				Needs.Meters.Add(Meter);
			}
		}
		Needs.MarkArrayDirty();

		// Initialize critical-edge tracking so a need that starts low fires exactly once.
		CriticalState.Reset();
		for (const FSimAg_NeedMeter& Meter : Needs.Meters)
		{
			CriticalState.Add(Meter.Need, Meter.GetNormalized() <= Meter.CriticalThreshold);
		}
	}
}

void USimAg_NeedsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Only authority drains and replicates; clients purely observe.
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	// Drain every frame for smooth server-side values, but only FLUSH replication on the cadence so a
	// crowd of agents doesn't spam the network. Critical edges are still evaluated every frame.
	DrainNeeds(DeltaTime);
	EvaluateCriticalEdges();

	ReplicationAccumulator += DeltaTime;
	if (ReplicationAccumulator >= ReplicationCadence)
	{
		ReplicationAccumulator = 0.f;
		// Mark each meter dirty so the fast array delta-serializes the accumulated drain this flush.
		for (FSimAg_NeedMeter& Meter : Needs.Meters)
		{
			Needs.MarkItemDirty(Meter);
		}
	}
}

void USimAg_NeedsComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(USimAg_NeedsComponent, Needs);
}

//~ ISeam_NeedProvider ----------------------------------------------------------------------------

float USimAg_NeedsComponent::GetNeedNormalized_Implementation(FGameplayTag NeedTag) const
{
	const FSimAg_NeedMeter* Meter = FindMeter(NeedTag);
	return Meter ? Meter->GetNormalized() : 0.f;
}

bool USimAg_NeedsComponent::SupportsNeed_Implementation(FGameplayTag NeedTag) const
{
	return FindMeter(NeedTag) != nullptr;
}

void USimAg_NeedsComponent::GetSupportedNeeds_Implementation(FGameplayTagContainer& OutNeeds) const
{
	for (const FSimAg_NeedMeter& Meter : Needs.Meters)
	{
		if (Meter.Need.IsValid())
		{
			OutNeeds.AddTag(Meter.Need);
		}
	}
}

//~ Mutators (authority only) ---------------------------------------------------------------------

float USimAg_NeedsComponent::ApplyNeedDelta(FGameplayTag NeedTag, float Delta)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		// Clients observe only; return the last replicated value for convenience.
		const FSimAg_NeedMeter* Meter = FindMeter(NeedTag);
		return Meter ? Meter->GetNormalized() : 0.f;
	}

	FSimAg_NeedMeter* Meter = FindMeter(NeedTag);
	if (!Meter)
	{
		return 0.f;
	}

	Meter->Current = FMath::Clamp(Meter->Current + Delta, 0.f, Meter->Max);
	MarkMeterDirty(*Meter);
	EvaluateCriticalEdges();
	return Meter->GetNormalized();
}

void USimAg_NeedsComponent::SetNeedValue(FGameplayTag NeedTag, float NewValue)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	FSimAg_NeedMeter* Meter = FindMeter(NeedTag);
	if (!Meter)
	{
		return;
	}
	Meter->Current = FMath::Clamp(NewValue, 0.f, Meter->Max);
	MarkMeterDirty(*Meter);
	EvaluateCriticalEdges();
}

void USimAg_NeedsComponent::AddOrUpdateNeed(const FSimAg_NeedMeter& InMeter)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	if (!InMeter.Need.IsValid())
	{
		return;
	}

	if (FSimAg_NeedMeter* Existing = FindMeter(InMeter.Need))
	{
		*Existing = InMeter;
		Existing->Current = FMath::Clamp(Existing->Current, 0.f, Existing->Max);
		MarkMeterDirty(*Existing);
	}
	else
	{
		FSimAg_NeedMeter Meter = InMeter;
		Meter.Current = FMath::Clamp(Meter.Current, 0.f, Meter.Max);
		FSimAg_NeedMeter& Added = Needs.Meters.Add_GetRef(Meter);
		CriticalState.Add(Added.Need, Added.GetNormalized() <= Added.CriticalThreshold);
		MarkMeterDirty(Added);
	}
	EvaluateCriticalEdges();
}

bool USimAg_NeedsComponent::IsNeedCritical(FGameplayTag NeedTag) const
{
	const FSimAg_NeedMeter* Meter = FindMeter(NeedTag);
	return Meter && Meter->GetNormalized() <= Meter->CriticalThreshold;
}

//~ Internals -------------------------------------------------------------------------------------

FSimAg_NeedMeter* USimAg_NeedsComponent::FindMeter(const FGameplayTag& NeedTag)
{
	return Needs.Meters.FindByPredicate([&NeedTag](const FSimAg_NeedMeter& M) { return M.Need == NeedTag; });
}

const FSimAg_NeedMeter* USimAg_NeedsComponent::FindMeter(const FGameplayTag& NeedTag) const
{
	return Needs.Meters.FindByPredicate([&NeedTag](const FSimAg_NeedMeter& M) { return M.Need == NeedTag; });
}

void USimAg_NeedsComponent::DrainNeeds(float DeltaSeconds)
{
	const float Scale = GetClockTimeScale();
	if (Scale <= 0.f)
	{
		return; // paused / stopped sim time => no drain
	}
	const float Scaled = DeltaSeconds * Scale;

	for (FSimAg_NeedMeter& Meter : Needs.Meters)
	{
		if (Meter.DrainPerSecond > 0.f && Meter.Current > 0.f)
		{
			Meter.Current = FMath::Max(0.f, Meter.Current - Meter.DrainPerSecond * Scaled);
		}
	}

	// Local changed delegate fires on the server each frame; clients fire from replication callbacks.
	OnNeedsChanged.Broadcast(this);
}

void USimAg_NeedsComponent::MarkMeterDirty(FSimAg_NeedMeter& Meter)
{
	Needs.MarkItemDirty(Meter);
	OnNeedsChanged.Broadcast(this);
}

void USimAg_NeedsComponent::EvaluateCriticalEdges()
{
	for (const FSimAg_NeedMeter& Meter : Needs.Meters)
	{
		const float Normalized = Meter.GetNormalized();
		const bool bNowCritical = Normalized <= Meter.CriticalThreshold;

		bool& bWasCritical = CriticalState.FindOrAdd(Meter.Need);
		if (bNowCritical && !bWasCritical)
		{
			// Downward edge: fire once.
			OnNeedCritical.Broadcast(Meter.Need, Normalized);

			if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
			{
				FSimAg_NeedEvent Event;
				Event.Need = Meter.Need;
				Event.Normalized = Normalized;

				FInstancedStruct Payload;
				Payload.InitializeAs<FSimAg_NeedEvent>(Event);
				Bus->BroadcastPayload(SimAgNativeTags::Bus_NeedCritical, Payload, GetOwner());
			}
		}
		bWasCritical = bNowCritical;
	}
}

float USimAg_NeedsComponent::GetClockTimeScale() const
{
	// Resolve the clock seam (service first, world subsystem fallback) and honour scale/pause.
	UObject* ClockObj = nullptr;
	if (UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		ClockObj = Locator->ResolveService(SimAgNativeTags::Service_Clock);
	}
	if (!ClockObj)
	{
		ClockObj = FDP_SubsystemStatics::GetWorldSubsystem<USimAg_ClockSubsystem>(this);
	}

	if (ClockObj && ClockObj->GetClass()->ImplementsInterface(USeam_SimClock::StaticClass()))
	{
		if (ISeam_SimClock::Execute_IsPaused(ClockObj))
		{
			return 0.f;
		}
		return static_cast<float>(ISeam_SimClock::Execute_GetTimeScale(ClockObj));
	}
	// No clock available: drain in real time.
	return 1.f;
}

void USimAg_NeedsComponent::HandleReplicatedChange()
{
	// Client-side: a replicated meter changed; surface it and re-evaluate critical edges locally so
	// client UI/AI see OnNeedCritical without the server needing to RPC it.
	EvaluateCriticalEdges();
	OnNeedsChanged.Broadcast(this);
}
