// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Clock/SimAg_ClockSubsystem.h"
#include "Settings/SimAg_DeveloperSettings.h"
#include "DesignPatternsSimAgentsTags.h"
#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Services/DPServiceTypes.h"
#include "Engine/World.h"
#include "UObject/UObjectThreadContext.h"

void USimAg_ClockSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Cache calendar shape from settings; never trust a zero/negative day length.
	if (const USimAg_DeveloperSettings* Settings = USimAg_DeveloperSettings::Get())
	{
		HoursPerDay = FMath::Max(1, Settings->DefaultHoursPerDay);
	}
	DaysPerSeason = FMath::Max(1, DaysPerSeason);

	bOwnsTime = true;
	FractionalDays = 0.0;
	LastObservedDay = 0;
	LastObservedHour = 0;

	// In owned mode on authority we advance time ourselves; clients wait for SyncFromServer.
	EnsureTickerRunning();

	// Publish ourselves as the clock seam so any consumer can resolve it by stable tag.
	RegisterClockService();

	UE_LOG(LogDP, Log, TEXT("[SimAg.Clock] Initialized (HoursPerDay=%d, authority=%s, owned=%s)."),
		HoursPerDay, HasWorldAuthority() ? TEXT("yes") : TEXT("no"), bOwnsTime ? TEXT("yes") : TEXT("no"));
}

void USimAg_ClockSubsystem::Deinitialize()
{
	// The ticker MUST be removed on teardown or it dangles past world destruction.
	StopTicker();

	// Drop our service registration if we still own it (best-effort; locator prunes stale anyway).
	if (UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		if (Locator->ResolveService(SimAgNativeTags::Service_Clock) == this)
		{
			Locator->UnregisterService(SimAgNativeTags::Service_Clock);
		}
	}

	ExternalTimeSource = nullptr;

	Super::Deinitialize();
}

void USimAg_ClockSubsystem::RegisterClockService()
{
	if (UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		// WeakObserved: the locator is GameInstance-scoped and survives level travel, so it must NOT
		// keep a dead world's subsystem alive. We allow override so a re-created world clock replaces
		// a pruned stale slot cleanly.
		Locator->RegisterService(SimAgNativeTags::Service_Clock, this, EDP_ServiceLifetime::WeakObserved, /*bAllowOverride=*/true);
	}
}

//~ ISeam_SimClock --------------------------------------------------------------------------------

double USimAg_ClockSubsystem::GetTimeScale_Implementation() const
{
	// While an external source owns time, our local scale is meaningless; report 1 (real-time) so a
	// consumer driving its own accumulator from a source-derived clock doesn't double-apply scaling.
	return ExternalTimeSource ? 1.0 : static_cast<double>(TimeScale);
}

bool USimAg_ClockSubsystem::IsPaused_Implementation() const
{
	// An external source has no pause concept here; treat derived mode as never-paused.
	return ExternalTimeSource ? false : bPaused;
}

float USimAg_ClockSubsystem::GetNormalizedTimeOfDay_Implementation() const
{
	return ComputeNormalizedTimeOfDay();
}

int32 USimAg_ClockSubsystem::GetDayNumber_Implementation() const
{
	return ComputeDayNumber();
}

//~ Time source binding ---------------------------------------------------------------------------

void USimAg_ClockSubsystem::SetExternalTimeSource(const TScriptInterface<ISimAg_TimeSource>& InSource)
{
	const bool bWasBound = (ExternalTimeSource.GetObject() != nullptr);

	if (InSource.GetObject() != nullptr)
	{
		ExternalTimeSource = InSource;
		bOwnsTime = false;
		// In derived mode we run NO ticker — the source advances on its own and we read it on demand.
		StopTicker();
		UE_LOG(LogDP, Log, TEXT("[SimAg.Clock] Bound external time source '%s' (derived mode)."),
			*GetNameSafe(InSource.GetObject()));
	}
	else
	{
		ExternalTimeSource = nullptr;
		bOwnsTime = true;
		// Seed owned time from wherever we currently are so unbinding doesn't snap the calendar back.
		FractionalDays = static_cast<double>(LastObservedDay)
			+ static_cast<double>(LastObservedHour) / static_cast<double>(FMath::Max(1, HoursPerDay));
		EnsureTickerRunning();
		if (bWasBound)
		{
			UE_LOG(LogDP, Log, TEXT("[SimAg.Clock] Unbound external time source (owned mode)."));
		}
	}

	// Re-evaluate edges immediately so listeners see the post-binding time without waiting a tick.
	DetectAndBroadcastEdges();
}

bool USimAg_ClockSubsystem::HasExternalTimeSource() const
{
	return ExternalTimeSource.GetObject() != nullptr;
}

//~ Authority mutators ----------------------------------------------------------------------------

void USimAg_ClockSubsystem::SetTimeScale(float InTimeScale)
{
	// Authority guard at the very top: clients never mutate the authoritative clock.
	if (!HasWorldAuthority())
	{
		return;
	}
	if (ExternalTimeSource)
	{
		// The external source owns speed; ignore to avoid a confusing double-scale.
		return;
	}
	TimeScale = FMath::Max(0.f, InTimeScale);
}

void USimAg_ClockSubsystem::SetPaused(bool bInPaused)
{
	if (!HasWorldAuthority())
	{
		return;
	}
	if (ExternalTimeSource)
	{
		return;
	}
	bPaused = bInPaused;
}

//~ Calendar reads --------------------------------------------------------------------------------

float USimAg_ClockSubsystem::ComputeNormalizedTimeOfDay() const
{
	if (ExternalTimeSource)
	{
		if (UObject* Obj = ExternalTimeSource.GetObject())
		{
			const float T = ISimAg_TimeSource::Execute_GetNormalizedTimeOfDay(Obj);
			return FMath::Frac(FMath::Max(0.f, T)); // clamp into [0,1)
		}
		return 0.f;
	}
	// Owned mode: fractional part of the day accumulator is the time of day.
	return static_cast<float>(FractionalDays - FMath::FloorToDouble(FractionalDays));
}

int32 USimAg_ClockSubsystem::ComputeDayNumber() const
{
	if (ExternalTimeSource)
	{
		if (UObject* Obj = ExternalTimeSource.GetObject())
		{
			return FMath::Max(0, ISimAg_TimeSource::Execute_GetDayNumber(Obj));
		}
		return 0;
	}
	return static_cast<int32>(FMath::FloorToDouble(FractionalDays));
}

FSimAg_DateTime USimAg_ClockSubsystem::GetDateTime() const
{
	const int32 Day = ComputeDayNumber();
	const float Tod = ComputeNormalizedTimeOfDay();
	const float HourF = Tod * static_cast<float>(HoursPerDay);
	const int32 Hour = FMath::Clamp(FMath::FloorToInt(HourF), 0, HoursPerDay - 1);
	const int32 Minute = FMath::Clamp(FMath::FloorToInt((HourF - static_cast<float>(Hour)) * 60.f), 0, 59);
	return FSimAg_DateTime(Day, Hour, Minute);
}

int32 USimAg_ClockSubsystem::GetSeasonNumber() const
{
	return ComputeDayNumber() / FMath::Max(1, DaysPerSeason);
}

FSimAg_ClockSnapshot USimAg_ClockSubsystem::MakeSnapshot() const
{
	FSimAg_ClockSnapshot Snap;
	Snap.DayNumber = ComputeDayNumber();
	Snap.NormalizedTimeOfDay = ComputeNormalizedTimeOfDay();
	Snap.TimeScale = static_cast<float>(GetTimeScale_Implementation());
	Snap.bPaused = IsPaused_Implementation();
	return Snap;
}

//~ Client sync -----------------------------------------------------------------------------------

void USimAg_ClockSubsystem::SyncFromServer(const FSimAg_ClockSnapshot& Snapshot)
{
	// Authority is its own source of truth — applying a snapshot there would fight the ticker.
	if (HasWorldAuthority())
	{
		return;
	}

	// Adopt the authoritative time exactly; clients then extrapolate locally between syncs using the
	// snapshot's scale/pause (the client ticker, registered below, advances FractionalDays).
	TimeScale = FMath::Max(0.f, Snapshot.TimeScale);
	bPaused = Snapshot.bPaused;
	FractionalDays = static_cast<double>(Snapshot.DayNumber)
		+ static_cast<double>(FMath::Frac(FMath::Max(0.f, Snapshot.NormalizedTimeOfDay)));

	// A client extrapolates owned-style between syncs only when no external source is driving it.
	if (!ExternalTimeSource)
	{
		bOwnsTime = true;
		EnsureTickerRunning();
	}

	DetectAndBroadcastEdges();
}

//~ Ticker ----------------------------------------------------------------------------------------

void USimAg_ClockSubsystem::EnsureTickerRunning()
{
	if (TickerHandle.IsValid())
	{
		return; // already running
	}
	if (ExternalTimeSource)
	{
		return; // derived mode runs no ticker
	}
	// Authority advances time; a client only extrapolates between server syncs. Both register a
	// ticker (the client's is reseeded by SyncFromServer), but a dedicated server with no client
	// sync still needs to advance, and a pure client must extrapolate — so both run the ticker.
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &USimAg_ClockSubsystem::TickClock));
}

void USimAg_ClockSubsystem::StopTicker()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
}

bool USimAg_ClockSubsystem::TickClock(float RealDeltaSeconds)
{
	// Defensive: if a source got bound after the ticker started, stop advancing owned time.
	if (ExternalTimeSource)
	{
		StopTicker();
		DetectAndBroadcastEdges();
		return false; // unregister this ticker
	}

	if (!bPaused && TimeScale > 0.f)
	{
		// Advance the fractional-day accumulator. One real second scaled by TimeScale advances
		// (TimeScale seconds) of sim time; converting to days uses HoursPerDay * 3600 sim-seconds/day.
		const double SimSecondsPerDay = static_cast<double>(HoursPerDay) * 3600.0;
		const double DeltaDays = (static_cast<double>(RealDeltaSeconds) * static_cast<double>(TimeScale)) / SimSecondsPerDay;
		FractionalDays += DeltaDays;
	}

	DetectAndBroadcastEdges();
	return true; // keep ticking
}

void USimAg_ClockSubsystem::DetectAndBroadcastEdges()
{
	const int32 Day = ComputeDayNumber();
	const float Tod = ComputeNormalizedTimeOfDay();
	const int32 Hour = FMath::Clamp(FMath::FloorToInt(Tod * static_cast<float>(HoursPerDay)), 0, HoursPerDay - 1);

	const bool bDayChanged = (Day != LastObservedDay);
	const bool bHourChanged = (Hour != LastObservedHour) || bDayChanged;

	if (bDayChanged)
	{
		LastObservedDay = Day;
		OnDayChanged.Broadcast(Day);
	}
	if (bHourChanged)
	{
		LastObservedHour = Hour;
		OnHourChanged.Broadcast(Day, Hour);
	}
}

//~ Debug -----------------------------------------------------------------------------------------

FString USimAg_ClockSubsystem::GetDPDebugString_Implementation() const
{
	const FSimAg_DateTime Now = GetDateTime();
	return FString::Printf(TEXT("SimAg.Clock %s | scale=%.2f%s | %s | season=%d"),
		*Now.ToString(),
		static_cast<float>(GetTimeScale_Implementation()),
		IsPaused_Implementation() ? TEXT(" PAUSED") : TEXT(""),
		ExternalTimeSource ? TEXT("derived") : TEXT("owned"),
		GetSeasonNumber());
}
