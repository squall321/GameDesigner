// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Schedule/SimAg_ScheduleComponent.h"
#include "Clock/SimAg_ClockSubsystem.h"
#include "DesignPatternsSimAgentsTags.h"
#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Data/DPDataRegistrySubsystem.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/Actor.h"

#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

USimAg_ScheduleComponent::USimAg_ScheduleComponent()
{
	PrimaryComponentTick.bCanEverTick = false; // event-driven (hour edges), never per-frame
	SetIsReplicatedByDefault(true);
}

void USimAg_ScheduleComponent::BeginPlay()
{
	Super::BeginPlay();
	BindClockDelegates();
	// Resolve once at start so the agent has a valid activity before the first hour edge.
	RefreshFromClock();
}

void USimAg_ScheduleComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindClockDelegates();
	Super::EndPlay(EndPlayReason);
}

void USimAg_ScheduleComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(USimAg_ScheduleComponent, ScheduleTag);
}

//~ ISimAg_Scheduler ------------------------------------------------------------------------------

FGameplayTag USimAg_ScheduleComponent::GetCurrentActivity_Implementation() const
{
	return CurrentActivity;
}

FSimAg_ScheduleEntry USimAg_ScheduleComponent::GetScheduleEntryForHour_Implementation(float HourOfDay) const
{
	// Resolve from the cached asset if available; const path must not lazy-load, so just read cache.
	if (ResolvedSchedule)
	{
		return ResolvedSchedule->ResolveActivityForHour(HourOfDay);
	}
	return FSimAg_ScheduleEntry();
}

//~ Authoritative assignment ----------------------------------------------------------------------

void USimAg_ScheduleComponent::SetScheduleTag(FGameplayTag InScheduleTag)
{
	// Authority guard at the top: only the server assigns the authoritative schedule.
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	if (ScheduleTag == InScheduleTag)
	{
		return;
	}
	ScheduleTag = InScheduleTag;
	ResolvedSchedule = nullptr; // force re-resolve against the new tag
	RefreshFromClock();
}

void USimAg_ScheduleComponent::OnRep_ScheduleTag()
{
	// The authoritative assignment changed; clients drop their cached asset and re-derive locally.
	ResolvedSchedule = nullptr;
	RefreshFromClock();
}

//~ Resolution ------------------------------------------------------------------------------------

void USimAg_ScheduleComponent::RefreshFromClock()
{
	USimAg_ClockSubsystem* Clock = GetClock();
	USimAg_ScheduleAsset* Schedule = GetResolvedSchedule();
	if (!Clock || !Schedule)
	{
		// No clock or no schedule: clear to an unscheduled state if we weren't already.
		if (CurrentActivity.IsValid() || CurrentLocation.IsValid())
		{
			ApplyResolvedEntry(FSimAg_ScheduleEntry());
		}
		return;
	}

	const FSimAg_DateTime Now = Clock->GetDateTime();
	// Use a fractional hour so sub-hour StartHours resolve precisely.
	const float HourOfDay = static_cast<float>(Now.Hour) + static_cast<float>(Now.Minute) / 60.f;
	const FSimAg_ScheduleEntry Entry = Schedule->ResolveActivityForHour(HourOfDay);
	ApplyResolvedEntry(Entry);
}

void USimAg_ScheduleComponent::ApplyResolvedEntry(const FSimAg_ScheduleEntry& Entry)
{
	if (Entry.Activity == CurrentActivity && Entry.Location == CurrentLocation)
	{
		return; // no change
	}

	const FGameplayTag PreviousActivity = CurrentActivity;
	CurrentActivity = Entry.Activity;
	CurrentLocation = Entry.Location;

	// Local notification fires on both server and clients so AI/UI react everywhere.
	OnActivityChanged.Broadcast(CurrentActivity, CurrentLocation);

	// The canonical cross-system event is broadcast only by authority to avoid duplicate bus traffic.
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
		{
			FSimAg_ActivityEvent Event;
			Event.NewActivity = CurrentActivity;
			Event.PreviousActivity = PreviousActivity;
			Event.NewLocation = CurrentLocation;

			FInstancedStruct Payload;
			Payload.InitializeAs<FSimAg_ActivityEvent>(Event);
			Bus->BroadcastPayload(SimAgNativeTags::Bus_ActivityChanged, Payload, GetOwner());
		}
	}
}

//~ Clock plumbing --------------------------------------------------------------------------------

void USimAg_ScheduleComponent::HandleHourChanged(int32 /*NewDay*/, int32 /*NewHour*/)
{
	RefreshFromClock();
}

USimAg_ClockSubsystem* USimAg_ScheduleComponent::GetClock() const
{
	if (CachedClock.IsValid())
	{
		return CachedClock.Get();
	}

	USimAg_ClockSubsystem* Clock = nullptr;

	// Prefer the stable service-locator key (the clock registers itself under Service.Clock).
	if (UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		Clock = Cast<USimAg_ClockSubsystem>(Locator->ResolveService(SimAgNativeTags::Service_Clock));
	}
	// Fall back to the world subsystem directly if the service wasn't registered yet.
	if (!Clock)
	{
		Clock = FDP_SubsystemStatics::GetWorldSubsystem<USimAg_ClockSubsystem>(this);
	}

	const_cast<USimAg_ScheduleComponent*>(this)->CachedClock = Clock;
	return Clock;
}

USimAg_ScheduleAsset* USimAg_ScheduleComponent::GetResolvedSchedule()
{
	if (ResolvedSchedule)
	{
		return ResolvedSchedule;
	}
	if (!ScheduleTag.IsValid())
	{
		return nullptr;
	}
	if (UDP_DataRegistrySubsystem* Registry = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_DataRegistrySubsystem>(this))
	{
		ResolvedSchedule = Registry->Find<USimAg_ScheduleAsset>(ScheduleTag);
	}
	return ResolvedSchedule;
}

void USimAg_ScheduleComponent::BindClockDelegates()
{
	if (USimAg_ClockSubsystem* Clock = GetClock())
	{
		// Idempotent: AddUniqueDynamic won't double-bind.
		Clock->OnHourChanged.AddUniqueDynamic(this, &USimAg_ScheduleComponent::HandleHourChanged);
	}
}

void USimAg_ScheduleComponent::UnbindClockDelegates()
{
	if (CachedClock.IsValid())
	{
		CachedClock->OnHourChanged.RemoveDynamic(this, &USimAg_ScheduleComponent::HandleHourChanged);
	}
}
