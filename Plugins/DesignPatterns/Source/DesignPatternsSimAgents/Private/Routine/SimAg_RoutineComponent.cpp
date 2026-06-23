// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Routine/SimAg_RoutineComponent.h"
#include "Schedule/SimAg_ScheduleComponent.h"
#include "Brain/SimAg_Agent.h"
#include "Clock/SimAg_ClockSubsystem.h"
#include "Settings/SimAg_DeveloperSettings.h"
#include "DesignPatternsSimAgentsTags.h"
#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Data/DPDataRegistrySubsystem.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/Actor.h"
#include "Schedule/SimAg_ScheduleAsset.h"

#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

USimAg_RoutineComponent::USimAg_RoutineComponent()
{
	PrimaryComponentTick.bCanEverTick = false; // event-driven on hour edges
	SetIsReplicatedByDefault(true);
}

void USimAg_RoutineComponent::BeginPlay()
{
	Super::BeginPlay();

	if (const USimAg_DeveloperSettings* Settings = USimAg_DeveloperSettings::Get())
	{
		TravelSpeedEstimate = FMath::Max(1.f, Settings->RoutineTravelSpeedEstimate);
	}

	BindClockDelegates();
	RefreshFromClock();
}

void USimAg_RoutineComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindClockDelegates();
	Super::EndPlay(EndPlayReason);
}

void USimAg_RoutineComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(USimAg_RoutineComponent, RoutineTag);
	DOREPLIFETIME(USimAg_RoutineComponent, bInterrupted);
}

//~ Authoritative assignment ----------------------------------------------------------------------

void USimAg_RoutineComponent::SetRoutineTag(FGameplayTag InRoutineTag)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	if (RoutineTag == InRoutineTag)
	{
		return;
	}
	RoutineTag = InRoutineTag;
	ResolvedRoutine = nullptr;
	RefreshFromClock();
}

bool USimAg_RoutineComponent::Interrupt(FGameplayTag Reason)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}
	if (bInterrupted)
	{
		return false; // already interrupted
	}
	// A non-interruptible step is held even under a flee request.
	if (!CurrentStep.bInterruptible)
	{
		return false;
	}

	SavedStep = CurrentStep;
	bHasSavedStep = true;
	bInterrupted = true;
	UE_LOG(LogDP, Verbose, TEXT("SimAg routine interrupted (reason=%s)."), *Reason.ToString());
	return true;
}

void USimAg_RoutineComponent::Resume()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	if (!bInterrupted)
	{
		return;
	}
	bInterrupted = false;
	// Re-derive the step for the CURRENT time (the routine may have advanced while fleeing), rather than
	// blindly restoring the saved step — but if the saved step is still the active one, this matches it.
	bHasSavedStep = false;
	RefreshFromClock();
}

//~ Resolution ------------------------------------------------------------------------------------

void USimAg_RoutineComponent::RefreshFromClock()
{
	// While interrupted, leave CurrentStep frozen (the flee behaviour owns the agent's targeting).
	if (bInterrupted)
	{
		return;
	}

	USimAg_RoutineAsset* Routine = GetResolvedRoutine();
	USimAg_ClockSubsystem* Clock = GetClock();
	if (!Routine || !Clock)
	{
		return;
	}

	const int32 HoursPerDay = Clock->GetHoursPerDay();
	const double NowDays = static_cast<double>(Clock->GetDayNumber_Implementation())
		+ static_cast<double>(Clock->GetNormalizedTimeOfDay_Implementation());

	// Travel lead-time: if the upcoming step is close in time and its location is far, switch to the
	// upcoming step early so the agent ARRIVES on the hour rather than starting to walk then.
	FSimAg_RoutineStep Active = Routine->ResolveStepForTime(NowDays, HoursPerDay);

	FSimAg_RoutineStep Upcoming;
	float HoursUntil = 0.f;
	if (Routine->ResolveUpcomingStep(NowDays, HoursPerDay, Upcoming, HoursUntil))
	{
		// Convert the agent's distance to the upcoming step's location into a travel time and compare it to
		// the time remaining. Location-tag -> world position is project-defined; we approximate using the
		// agent's current location vs. the active step (a conservative "do we need to leave now?" test that
		// errs toward leaving slightly early, which is the desired daily-life feel).
		if (const AActor* Owner = GetOwner())
		{
			// Hours-to-seconds via the clock's day length: seconds per in-sim hour. We use the time scale
			// only indirectly (the hour edge cadence already accounts for it); the lead decision is in hours.
			const float HoursOfTravelNeeded = EstimateTravelHours(Owner->GetActorLocation(), HoursPerDay, Clock);
			if (HoursUntil <= HoursOfTravelNeeded)
			{
				Active = Upcoming; // leave early toward the upcoming step
			}
		}
	}

	ApplyResolvedStep(Active);
}

void USimAg_RoutineComponent::ApplyResolvedStep(const FSimAg_RoutineStep& Step)
{
	if (Step.Activity == CurrentStep.Activity && Step.Location == CurrentStep.Location)
	{
		return;
	}
	CurrentStep = Step;

	OnRoutineStepChanged.Broadcast(CurrentStep.Activity, CurrentStep.Location);

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
		{
			FSimAg_ActivityEvent Event;
			Event.NewActivity = CurrentStep.Activity;
			Event.NewLocation = CurrentStep.Location;

			FInstancedStruct Payload;
			Payload.InitializeAs<FSimAg_ActivityEvent>(Event);
			Bus->BroadcastPayload(SimAgNativeTags::Bus_ActivityChanged, Payload, GetOwner());
		}
	}
}

//~ Clock plumbing --------------------------------------------------------------------------------

void USimAg_RoutineComponent::HandleHourChanged(int32 /*NewDay*/, int32 /*NewHour*/)
{
	RefreshFromClock();
}

void USimAg_RoutineComponent::OnRep_Routine()
{
	ResolvedRoutine = nullptr;
	RefreshFromClock();
}

USimAg_ClockSubsystem* USimAg_RoutineComponent::GetClock() const
{
	if (CachedClock.IsValid())
	{
		return CachedClock.Get();
	}
	USimAg_ClockSubsystem* Clock = nullptr;
	if (UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		Clock = Cast<USimAg_ClockSubsystem>(Locator->ResolveService(SimAgNativeTags::Service_Clock));
	}
	if (!Clock)
	{
		Clock = FDP_SubsystemStatics::GetWorldSubsystem<USimAg_ClockSubsystem>(this);
	}
	const_cast<USimAg_RoutineComponent*>(this)->CachedClock = Clock;
	return Clock;
}

USimAg_RoutineAsset* USimAg_RoutineComponent::GetResolvedRoutine()
{
	if (ResolvedRoutine)
	{
		return ResolvedRoutine;
	}
	if (!RoutineTag.IsValid())
	{
		return nullptr;
	}
	if (UDP_DataRegistrySubsystem* Registry = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_DataRegistrySubsystem>(this))
	{
		ResolvedRoutine = Registry->Find<USimAg_RoutineAsset>(RoutineTag);
	}
	return ResolvedRoutine;
}

void USimAg_RoutineComponent::BindClockDelegates()
{
	if (USimAg_ClockSubsystem* Clock = GetClock())
	{
		Clock->OnHourChanged.AddUniqueDynamic(this, &USimAg_RoutineComponent::HandleHourChanged);
	}
}

void USimAg_RoutineComponent::UnbindClockDelegates()
{
	if (USimAg_ClockSubsystem* Clock = CachedClock.Get())
	{
		Clock->OnHourChanged.RemoveDynamic(this, &USimAg_RoutineComponent::HandleHourChanged);
	}
}

USimAg_ScheduleComponent* USimAg_RoutineComponent::GetScheduleComponent() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<USimAg_ScheduleComponent>() : nullptr;
}

float USimAg_RoutineComponent::EstimateTravelHours(const FVector& AgentLocation, int32 HoursPerDay, const USimAg_ClockSubsystem* Clock) const
{
	if (!Clock)
	{
		return 0.f;
	}
	// Distance to the agent's next anchor. The routine step's Location is a tag with no inherent world
	// position, so we use the agent's work anchor (where most routine steps send it) via the agent seam; a
	// project wanting per-location positions supplies them through the agent's anchors.
	FVector AnchorLocation = AgentLocation;
	if (const AActor* Owner = GetOwner())
	{
		if (Owner->Implements<USimAg_Agent>())
		{
			AnchorLocation = ISimAg_Agent::Execute_GetWorkLocation(const_cast<AActor*>(Owner));
		}
	}
	const float Distance = static_cast<float>(FVector::Dist(AgentLocation, AnchorLocation));
	if (Distance <= KINDA_SMALL_NUMBER)
	{
		return 0.f;
	}

	// Real seconds of travel = distance / speed. Convert to in-sim HOURS:
	//   the clock advances FractionalDays by RealDelta * TimeScale (1.0 day per real second at scale 1),
	//   so in-sim hours per real second = TimeScale * HoursPerDay.
	const float RealTravelSeconds = Distance / FMath::Max(1.f, TravelSpeedEstimate);
	const float HoursPerRealSecond = static_cast<float>(Clock->GetTimeScale_Implementation()) * static_cast<float>(FMath::Max(1, HoursPerDay));
	return RealTravelSeconds * HoursPerRealSecond;
}
