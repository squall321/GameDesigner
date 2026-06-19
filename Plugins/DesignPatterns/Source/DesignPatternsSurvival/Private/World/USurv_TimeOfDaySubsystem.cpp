// Copyright DesignPatterns plugin. All Rights Reserved.

#include "World/USurv_TimeOfDaySubsystem.h"
#include "Core/DPLog.h"
#include "Engine/World.h"

void USurv_TimeOfDaySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	CurrentPhase = PhaseFromNormalizedTime(NormalizedTimeOfDay);

	// Advance the clock once per frame on the authority. Using FTSTicker keeps the subsystem itself
	// non-tickable (no editor/seamless-travel ticking), mirroring the core message-bus pattern.
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &USurv_TimeOfDaySubsystem::Tick));
}

void USurv_TimeOfDaySubsystem::Deinitialize()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
	Super::Deinitialize();
}

bool USurv_TimeOfDaySubsystem::HasWorldAuthority() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	const ENetMode NetMode = World->GetNetMode();
	return NetMode != NM_Client; // server, listen-server host, or standalone
}

ESurv_DayPhase USurv_TimeOfDaySubsystem::PhaseFromNormalizedTime(float Normalized)
{
	const float T = FMath::Frac(Normalized < 0.f ? Normalized + 1.f : Normalized);
	if (T < 0.25f) return ESurv_DayPhase::Night; // 00:00-06:00
	if (T < 0.35f) return ESurv_DayPhase::Dawn;  // 06:00-08:24
	if (T < 0.70f) return ESurv_DayPhase::Day;   // 08:24-16:48
	if (T < 0.80f) return ESurv_DayPhase::Dusk;  // 16:48-19:12
	return ESurv_DayPhase::Night;                // 19:12-24:00
}

bool USurv_TimeOfDaySubsystem::Tick(float DeltaTime)
{
	if (!HasWorldAuthority() || DayLengthSeconds <= 0.f)
	{
		return true; // keep the ticker alive; clients/idle just don't advance here
	}

	const float Advance = DeltaTime / DayLengthSeconds;
	const float NewTime = NormalizedTimeOfDay + Advance;

	if (NewTime >= 1.f)
	{
		const int32 DaysCrossed = FMath::FloorToInt(NewTime);
		DayNumber += DaysCrossed;
		OnNewDay.Broadcast(DayNumber);
	}

	NormalizedTimeOfDay = FMath::Frac(NewTime);
	UpdatePhase();
	return true;
}

void USurv_TimeOfDaySubsystem::UpdatePhase()
{
	const ESurv_DayPhase NewPhase = PhaseFromNormalizedTime(NormalizedTimeOfDay);
	if (NewPhase != CurrentPhase)
	{
		CurrentPhase = NewPhase;
		UE_LOG(LogDP, Verbose, TEXT("[Survival] Day phase -> %d (t=%.3f)"), (int32)NewPhase, NormalizedTimeOfDay);
		OnPhaseChanged.Broadcast(NewPhase);
	}
}

void USurv_TimeOfDaySubsystem::SetDayLengthSeconds(float Seconds)
{
	if (!HasWorldAuthority())
	{
		return;
	}
	DayLengthSeconds = FMath::Max(1.f, Seconds);
}

void USurv_TimeOfDaySubsystem::SetNormalizedTimeOfDay(float NewValue)
{
	if (!HasWorldAuthority())
	{
		return;
	}
	NormalizedTimeOfDay = FMath::Frac(FMath::Max(0.f, NewValue));
	UpdatePhase();
}

void USurv_TimeOfDaySubsystem::SyncFromServer(float ServerNormalizedTime, int32 ServerDayNumber, float ServerDayLengthSeconds)
{
	// Client convergence path: adopt the server snapshot, then let the local clock continue.
	// (No-op on the authority, which is already the source of truth.)
	if (HasWorldAuthority())
	{
		return;
	}
	NormalizedTimeOfDay = FMath::Frac(FMath::Max(0.f, ServerNormalizedTime));
	DayNumber = FMath::Max(0, ServerDayNumber);
	DayLengthSeconds = FMath::Max(1.f, ServerDayLengthSeconds);
	UpdatePhase();
}

FString USurv_TimeOfDaySubsystem::GetDPDebugString_Implementation() const
{
	const TCHAR* PhaseName = TEXT("?");
	switch (CurrentPhase)
	{
	case ESurv_DayPhase::Dawn:  PhaseName = TEXT("Dawn");  break;
	case ESurv_DayPhase::Day:   PhaseName = TEXT("Day");   break;
	case ESurv_DayPhase::Dusk:  PhaseName = TEXT("Dusk");  break;
	case ESurv_DayPhase::Night: PhaseName = TEXT("Night"); break;
	}
	return FString::Printf(TEXT("TimeOfDay: Day %d, t=%.2f, %s"), DayNumber, NormalizedTimeOfDay, PhaseName);
}
