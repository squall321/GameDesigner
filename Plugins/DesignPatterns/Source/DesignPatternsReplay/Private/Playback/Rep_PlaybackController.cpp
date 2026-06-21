// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Playback/Rep_PlaybackController.h"
#include "Timeline/Rep_ReplayEvent.h"
#include "Settings/Rep_DeveloperSettings.h"
#include "Core/DPLog.h"

#include "Engine/World.h"
#include "Engine/DemoNetDriver.h"
#include "GameFramework/WorldSettings.h"

void URep_PlaybackController::BindToWorld(UWorld* InWorld)
{
	PlaybackWorld = InWorld;

	// Seed the cached speed from settings so a fresh controller starts at the configured rate.
	if (const URep_DeveloperSettings* Settings = URep_DeveloperSettings::Get())
	{
		CachedSpeed = ClampSpeed(Settings->DefaultPlaybackSpeed);
	}
	else
	{
		// Defensive fallback when the settings CDO is unavailable (e.g. very early load).
		CachedSpeed = 1.f;
	}
	bPaused = false;
}

UDemoNetDriver* URep_PlaybackController::GetDemoDriver() const
{
	if (const UWorld* World = PlaybackWorld.Get())
	{
		if (UDemoNetDriver* Demo = World->GetDemoNetDriver())
		{
			return Demo->IsPlaying() ? Demo : nullptr;
		}
	}
	return nullptr;
}

bool URep_PlaybackController::IsPlaybackActive() const
{
	return GetDemoDriver() != nullptr;
}

float URep_PlaybackController::ClampSpeed(float Speed) const
{
	float Min = 0.1f;
	float Max = 8.f;
	if (const URep_DeveloperSettings* Settings = URep_DeveloperSettings::Get())
	{
		Min = Settings->MinPlaybackSpeed;
		Max = Settings->MaxPlaybackSpeed;
	}
	return FMath::Clamp(Speed, Min, Max);
}

// ---------------------------------------------------------------------------------------------
// Transport
// ---------------------------------------------------------------------------------------------

void URep_PlaybackController::SetPlaybackSpeed(float Speed)
{
	// A request of (near) zero means pause; the demo driver does not meaningfully run at 0x.
	if (Speed <= KINDA_SMALL_NUMBER)
	{
		SetPaused(true);
		return;
	}

	CachedSpeed = ClampSpeed(Speed);
	bPaused = false;

	if (const UWorld* World = PlaybackWorld.Get())
	{
		if (AWorldSettings* WS = World->GetWorldSettings())
		{
			// DemoPlayTimeDilation is the engine's own replay speed control — we wrap it, not reinvent it.
			WS->DemoPlayTimeDilation = CachedSpeed;
		}
	}
	OnTransportChanged.Broadcast();
}

float URep_PlaybackController::GetPlaybackSpeed() const
{
	if (bPaused)
	{
		return 0.f;
	}
	if (const UWorld* World = PlaybackWorld.Get())
	{
		if (const AWorldSettings* WS = World->GetWorldSettings())
		{
			return WS->DemoPlayTimeDilation;
		}
	}
	return CachedSpeed;
}

void URep_PlaybackController::SetPaused(bool bInPaused)
{
	if (bPaused == bInPaused)
	{
		return;
	}
	bPaused = bInPaused;

	if (const UWorld* World = PlaybackWorld.Get())
	{
		if (AWorldSettings* WS = World->GetWorldSettings())
		{
			// Pause == 0 time dilation; resume restores the cached speed. This mirrors the engine
			// replay-pause behaviour without touching the net stream.
			WS->DemoPlayTimeDilation = bPaused ? 0.f : CachedSpeed;
		}
	}
	OnTransportChanged.Broadcast();
}

bool URep_PlaybackController::IsPaused() const
{
	return bPaused;
}

void URep_PlaybackController::TogglePause()
{
	SetPaused(!bPaused);
}

// ---------------------------------------------------------------------------------------------
// Seeking
// ---------------------------------------------------------------------------------------------

void URep_PlaybackController::SeekToTime(float TimeSeconds)
{
	UDemoNetDriver* Demo = GetDemoDriver();
	if (!Demo)
	{
		UE_LOG(LogDP, Warning, TEXT("Replay: SeekToTime with no active playback."));
		return;
	}

	const float Total = GetTotalTime();
	const float Clamped = (Total > 0.f) ? FMath::Clamp(TimeSeconds, 0.f, Total) : FMath::Max(0.f, TimeSeconds);

	// WRAP: GotoTimeInSeconds is the engine's own scrub; it streams the needed checkpoint + data.
	Demo->GotoTimeInSeconds(Clamped);
	OnTransportChanged.Broadcast();
}

void URep_PlaybackController::SeekToEvent(const FRep_ReplayEvent& Event)
{
	float LeadIn = 1.5f;
	if (const URep_DeveloperSettings* Settings = URep_DeveloperSettings::Get())
	{
		LeadIn = Settings->SeekToEventLeadInSeconds;
	}
	const float Target = FMath::Max(0.f, Event.Time - LeadIn);
	SeekToTime(Target);
}

void URep_PlaybackController::Restart()
{
	SeekToTime(0.f);
}

// ---------------------------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------------------------

float URep_PlaybackController::GetCurrentTime() const
{
	if (const UDemoNetDriver* Demo = GetDemoDriver())
	{
		return Demo->GetDemoCurrentTime();
	}
	return 0.f;
}

float URep_PlaybackController::GetTotalTime() const
{
	if (const UDemoNetDriver* Demo = GetDemoDriver())
	{
		return Demo->GetDemoTotalTime();
	}
	return 0.f;
}

float URep_PlaybackController::GetNormalizedPosition() const
{
	const float Total = GetTotalTime();
	return (Total > KINDA_SMALL_NUMBER) ? FMath::Clamp(GetCurrentTime() / Total, 0.f, 1.f) : 0.f;
}
