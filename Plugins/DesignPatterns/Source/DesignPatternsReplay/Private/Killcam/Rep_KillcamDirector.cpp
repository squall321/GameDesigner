// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Killcam/Rep_KillcamDirector.h"
#include "Playback/Rep_PlaybackController.h"
#include "Spectator/Rep_SpectatorController.h"
#include "Settings/Rep_DeveloperSettings.h"
#include "Core/DPLog.h"

#include "Services/DPServiceLocatorSubsystem.h"
#include "Display/Seam_FloatingTextFeed.h"

#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"

namespace
{
	/** The Display-owned service key the HUD registers its floating-text overlay under. */
	static FGameplayTag GetFloatingTextServiceTag()
	{
		// Resolved by name (not declared here): the Display/HUD wave owns this key. ErrorIfNotFound
		// false so a project without the HUD overlay simply yields an invalid tag -> no-op feed.
		return FGameplayTag::RequestGameplayTag(FName("DP.Service.Display.FloatingText"), /*ErrorIfNotFound*/ false);
	}
}

void URep_KillcamDirector::Initialize(URep_PlaybackController* InPlayback, URep_SpectatorController* InSpectator, APlayerController* InLocalController)
{
	Playback = InPlayback;
	Spectator = InSpectator;
	LocalController = InLocalController;
}

void URep_KillcamDirector::WatchComponent(URep_KillcamComponent* Component)
{
	if (URep_KillcamComponent* Prev = Watched.Get())
	{
		Prev->OnKillcamDeath.RemoveDynamic(this, &URep_KillcamDirector::HandleDeath);
	}

	Watched = Component;
	if (Component)
	{
		Component->OnKillcamDeath.AddDynamic(this, &URep_KillcamDirector::HandleDeath);
	}
}

void URep_KillcamDirector::SetKillerActorForFraming(AActor* InKillerActor)
{
	KillerActor = InKillerActor;
}

void URep_KillcamDirector::HandleDeath(const FRep_KillcamRecord& Record)
{
	BeginKillcam(Record);
}

void URep_KillcamDirector::BeginKillcam(const FRep_KillcamRecord& Record)
{
	const URep_DeveloperSettings* Settings = URep_DeveloperSettings::Get();
	if (Settings && !Settings->bEnableKillcam)
	{
		return;
	}

	if (bActive)
	{
		// A new death during an active death-cam: end the current and restart on the newest.
		EndKillcam();
	}

	URep_PlaybackController* Controller = Playback.Get();
	if (!Controller || !Controller->IsPlaybackActive())
	{
		UE_LOG(LogDP, Verbose, TEXT("Killcam: no active playback; cannot run death-cam."));
		return;
	}

	if (!Record.IsValid())
	{
		return;
	}

	CurrentRecord = Record;

	// Lookback window from settings; clamp to [0, deathTime] so we never seek before the demo start.
	const float Lookback = Settings ? Settings->KillcamLookbackSeconds : 6.f;
	const float Speed    = Settings ? Settings->KillcamPlaybackSpeed   : 1.f;

	const float DeathTime = FMath::Max(0.f, Record.DeathTimeSeconds);
	const float StartTime = FMath::Max(0.f, DeathTime - FMath::Max(0.f, Lookback));
	ReturnAtTimeSeconds = DeathTime;

	// 1) rewind, 2) frame the killer, 3) play.
	Controller->SeekToTime(StartTime);
	Controller->SetPlaybackSpeed(Speed);
	Controller->SetPaused(false);

	if (URep_SpectatorController* Spec = Spectator.Get())
	{
		if (APlayerController* PC = LocalController.Get())
		{
			Spec->EnterSpectator(PC);
			if (Settings && Settings->bKillcamFollowKiller && KillerActor.IsValid())
			{
				Spec->FocusOnActor(KillerActor.Get());
			}
		}
	}

	// Re-surface the lethal hit as a floating damage number (cosmetic; no-op if no feed registered).
	EmitLethalFloatingText(Record);

	bActive = true;
	ElapsedSeconds = 0.f;

	OnKillcamStarted.Broadcast(CurrentRecord);
	UE_LOG(LogDP, Log, TEXT("Killcam: death-cam started [%.2f..%.2f] speed=%.2f."), StartTime, DeathTime, Speed);
}

void URep_KillcamDirector::Tick(float DeltaSeconds)
{
	if (!bActive)
	{
		return;
	}

	URep_PlaybackController* Controller = Playback.Get();
	if (!Controller || !Controller->IsPlaybackActive())
	{
		EndKillcam();
		return;
	}

	ElapsedSeconds += DeltaSeconds;

	// Detect the return-point from the REAL playback position; a generous watchdog handles a stuck seek.
	const float CurrentTime = Controller->GetCurrentTime();
	const URep_DeveloperSettings* Settings = URep_DeveloperSettings::Get();
	const float Lookback = Settings ? Settings->KillcamLookbackSeconds : 6.f;
	const float MaxWatch = Lookback + 30.f;

	if (CurrentTime >= ReturnAtTimeSeconds || ElapsedSeconds >= MaxWatch)
	{
		EndKillcam();
	}
}

void URep_KillcamDirector::EndKillcam()
{
	if (!bActive)
	{
		return;
	}

	bActive = false;
	ElapsedSeconds = 0.f;

	// Restore live: exit the spectator framing (returns the prior view target / pawn) and resume normal
	// playback speed. We leave playback running (returning "to live") rather than pausing.
	if (URep_SpectatorController* Spec = Spectator.Get())
	{
		Spec->ExitSpectator();
	}
	if (URep_PlaybackController* Controller = Playback.Get())
	{
		Controller->SetPlaybackSpeed(1.f);
	}

	OnKillcamEnded.Broadcast();
	UE_LOG(LogDP, Log, TEXT("Killcam: death-cam ended; returned to live."));
}

void URep_KillcamDirector::EmitLethalFloatingText(const FRep_KillcamRecord& Record)
{
	ISeam_FloatingTextFeed* Feed = ResolveFloatingTextFeed();
	if (!Feed)
	{
		return;
	}

	// Build a display string from the lethal magnitude (numeric variants only; else a generic label).
	FText Text;
	switch (Record.LethalMagnitude.Type)
	{
	case ESeam_NetValueType::Int:
		Text = FText::AsNumber(Record.LethalMagnitude.IntValue);
		break;
	case ESeam_NetValueType::Float:
		Text = FText::AsNumber(Record.LethalMagnitude.FloatValue);
		break;
	default:
		Text = NSLOCTEXT("Replay", "Killcam_Lethal", "Lethal");
		break;
	}

	UObject* FeedObj = Cast<UObject>(Feed);
	if (FeedObj)
	{
		// Style tag: a child of the project's floating-text tree; we pass the death cause if any.
		ISeam_FloatingTextFeed::Execute_PushFloatingText(FeedObj, Record.Victim, Text, Record.CauseTag, Record.LethalMagnitude);
	}
}

ISeam_FloatingTextFeed* URep_KillcamDirector::ResolveFloatingTextFeed() const
{
	// A plain UObject has no world of its own; resolve via the local player controller's world.
	const APlayerController* PC = LocalController.Get();
	const UWorld* World = PC ? PC->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}
	UGameInstance* GI = World->GetGameInstance();
	if (!GI)
	{
		return nullptr;
	}
	UDP_ServiceLocatorSubsystem* Locator = GI->GetSubsystem<UDP_ServiceLocatorSubsystem>();
	if (!Locator)
	{
		return nullptr;
	}

	const FGameplayTag Key = GetFloatingTextServiceTag();
	if (!Key.IsValid())
	{
		return nullptr;
	}

	UObject* Provider = Locator->ResolveService(Key);
	if (Provider && Provider->GetClass()->ImplementsInterface(USeam_FloatingTextFeed::StaticClass()))
	{
		return Cast<ISeam_FloatingTextFeed>(Provider);
	}
	return nullptr;
}
