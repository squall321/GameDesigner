// Copyright DesignPatterns plugin. All Rights Reserved.

#include "VO/Audio_VOSubsystem.h"
#include "VO/Audio_VOBankDataAsset.h"
#include "DesignPatternsAudioModule.h"
#include "Manager/Audio_SoundManagerSubsystem.h"
#include "Settings/Audio_DeveloperSettings.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Services/DPServiceTypes.h"
#include "MessageBus/DPMessageBusSubsystem.h"

#include "Engine/World.h"
#include "Sound/SoundBase.h"

// =====================================================================================================
// Lifecycle
// =====================================================================================================

bool UAudio_VOSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	return !IsRunningDedicatedServer();
}

void UAudio_VOSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	Collection.InitializeDependency(UDP_ServiceLocatorSubsystem::StaticClass());
	Collection.InitializeDependency(UDP_MessageBusSubsystem::StaticClass());

	const UWorld* World = GetWorld();
	bAudioAvailable = (World != nullptr) && (World->GetNetMode() != NM_DedicatedServer)
		&& (GEngine != nullptr) && GEngine->UseSound();

	LoadDefaultBanksFromSettings();
	RegisterAsService();

	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UAudio_VOSubsystem::Tick));

	UE_LOG(LogDP, Log, TEXT("Audio_VOSubsystem initialized (audio %s, %d bank(s))."),
		bAudioAvailable ? TEXT("available") : TEXT("unavailable"), LoadedBanks.Num());
}

void UAudio_VOSubsystem::Deinitialize()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}

	// Release any held duck so a torn-down VO subsystem cannot leave music/SFX ducked.
	if (ActiveDuckHandle.IsValid())
	{
		if (UAudio_SoundManagerSubsystem* Manager = FDP_SubsystemStatics::GetGameInstanceSubsystem<UAudio_SoundManagerSubsystem>(this))
		{
			Manager->ReleaseDuck(ActiveDuckHandle);
		}
		ActiveDuckHandle.Invalidate();
	}

	UnregisterAsService();

	Queue.Reset();
	ActiveEntry = FAudio_VOQueueEntry();
	bPlaying = false;
	AudioController = nullptr;
	BarkCooldownUntil.Reset();

	Super::Deinitialize();
}

// =====================================================================================================
// Service + banks
// =====================================================================================================

void UAudio_VOSubsystem::RegisterAsService()
{
	if (UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		Locator->RegisterService(AudioNativeTags::Service_AudioVO, this, EDP_ServiceLifetime::WeakObserved, /*bAllowOverride=*/true);
	}
}

void UAudio_VOSubsystem::UnregisterAsService()
{
	if (UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		Locator->UnregisterService(AudioNativeTags::Service_AudioVO);
	}
}

void UAudio_VOSubsystem::LoadDefaultBanksFromSettings()
{
	const UAudio_DeveloperSettings* Settings = UAudio_DeveloperSettings::Get();
	if (!Settings)
	{
		return;
	}
	for (const TSoftObjectPtr<UAudio_VOBankDataAsset>& SoftBank : Settings->DefaultVOBanks)
	{
		if (SoftBank.IsNull())
		{
			continue;
		}
		if (UAudio_VOBankDataAsset* Bank = SoftBank.LoadSynchronous())
		{
			AddVOBank(Bank);
		}
	}
}

void UAudio_VOSubsystem::AddVOBank(UAudio_VOBankDataAsset* Bank)
{
	if (!Bank || LoadedBanks.Contains(Bank))
	{
		return;
	}
	LoadedBanks.Add(Bank);
}

void UAudio_VOSubsystem::RemoveVOBank(UAudio_VOBankDataAsset* Bank)
{
	LoadedBanks.Remove(Bank);
}

const FAudio_VOEntry* UAudio_VOSubsystem::ResolveEntry(const FGameplayTag& LineTag) const
{
	for (const TObjectPtr<UAudio_VOBankDataAsset>& Bank : LoadedBanks)
	{
		if (!Bank)
		{
			continue;
		}
		if (const FAudio_VOEntry* Entry = Bank->FindLine(LineTag))
		{
			return Entry;
		}
	}
	return nullptr;
}

IAudio_AudioController* UAudio_VOSubsystem::ResolveAudioController()
{
	// Re-resolve if the cached interface went stale (e.g. manager replaced).
	if (AudioController.GetObject() != nullptr && AudioController.GetInterface() != nullptr)
	{
		return AudioController.GetInterface();
	}

	if (UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		if (UObject* Provider = Locator->ResolveService(AudioNativeTags::Service_Audio))
		{
			if (Provider->GetClass()->ImplementsInterface(UAudio_AudioController::StaticClass()))
			{
				AudioController.SetObject(Provider);
				AudioController.SetInterface(Cast<IAudio_AudioController>(Provider));
				return AudioController.GetInterface();
			}
		}
	}
	return nullptr;
}

// =====================================================================================================
// IAudio_VOController
// =====================================================================================================

FGuid UAudio_VOSubsystem::PlayVO_Implementation(const FAudio_VORequest& Request)
{
	if (!bAudioAvailable || !Request.LineTag.IsValid())
	{
		return FGuid();
	}

	const FAudio_VOEntry* Entry = ResolveEntry(Request.LineTag);
	if (!Entry)
	{
		UE_LOG(LogDP, Verbose, TEXT("VO: PlayVO for unresolved line '%s'."), *Request.LineTag.ToString());
		return FGuid();
	}

	const int32 Priority = (Request.PriorityOverride >= 0) ? Request.PriorityOverride : Entry->DefaultPriority;

	// Interrupt arbitration against the active line.
	if (bPlaying)
	{
		switch (Request.InterruptMode)
		{
		case EAudio_VOInterrupt::DropIfBusy:
			if (ActiveEntry.Priority >= Priority)
			{
				return FGuid(); // Something at least as important is playing; drop low-value chatter.
			}
			break;

		case EAudio_VOInterrupt::Interrupt:
			if (ActiveEntry.Priority <= Priority)
			{
				// Stop the active line; the new one will start in TryStartNext after we enqueue it.
				if (ActiveEntry.Category.IsValid())
				{
					if (IAudio_AudioController* Controller = ResolveAudioController())
					{
						Controller->Execute_StopCategory(Controller->_getUObject(), ActiveEntry.Category);
					}
				}
				FinishActive();
			}
			break;

		case EAudio_VOInterrupt::Queue:
		default:
			break;
		}
	}

	FAudio_VOQueueEntry QueueEntry;
	QueueEntry.Handle = FGuid::NewGuid();
	QueueEntry.Request = Request;
	QueueEntry.Priority = Priority;
	QueueEntry.Category = Entry->Category;
	QueueEntry.DuckBusTag = Entry->DuckBusTag;
	QueueEntry.Sequence = NextSequence++;

	const FGuid Result = QueueEntry.Handle;
	EnqueueOrdered(MoveTemp(QueueEntry));
	TryStartNext();
	return Result;
}

void UAudio_VOSubsystem::StopVO_Implementation(FGuid Handle)
{
	if (!Handle.IsValid())
	{
		return;
	}

	// Cancel a queued (not-yet-playing) line.
	const int32 QueuedIndex = Queue.IndexOfByPredicate(
		[&Handle](const FAudio_VOQueueEntry& E) { return E.Handle == Handle; });
	if (QueuedIndex != INDEX_NONE)
	{
		Queue.RemoveAt(QueuedIndex);
		return;
	}

	// Stop the active line.
	if (bPlaying && ActiveEntry.Handle == Handle)
	{
		if (ActiveEntry.Category.IsValid())
		{
			if (IAudio_AudioController* Controller = ResolveAudioController())
			{
				Controller->Execute_StopCategory(Controller->_getUObject(), ActiveEntry.Category);
			}
		}
		FinishActive();
		TryStartNext();
	}
}

bool UAudio_VOSubsystem::TryBark_Implementation(FGameplayTag BarkTag, FVector Location)
{
	if (!bAudioAvailable || !BarkTag.IsValid())
	{
		return false;
	}

	const FAudio_VOEntry* Entry = ResolveEntry(BarkTag);
	if (!Entry || !Entry->bIsBark)
	{
		return false; // Not a bark — TryBark only plays bark-flagged lines.
	}

	// Cooldown gate.
	const double Now = NowSeconds();
	if (const double* Until = BarkCooldownUntil.Find(BarkTag))
	{
		if (Now < *Until)
		{
			return false; // Still cooling down.
		}
	}
	BarkCooldownUntil.Add(BarkTag, Now + FMath::Max(0.f, Entry->BarkCooldownSeconds));

	// Barks are low-friction: drop if something more important is already speaking.
	FAudio_VORequest Request;
	Request.LineTag = BarkTag;
	Request.bAtLocation = true;
	Request.WorldLocation = Location;
	Request.InterruptMode = EAudio_VOInterrupt::DropIfBusy;
	Request.VolumeMult = 1.f;

	return PlayVO_Implementation(Request).IsValid();
}

void UAudio_VOSubsystem::StopAllVO(FGameplayTag Category)
{
	if (!Category.IsValid())
	{
		return;
	}

	Queue.RemoveAll([&Category](const FAudio_VOQueueEntry& E)
	{
		return E.Category.IsValid() && E.Category.MatchesTag(Category);
	});

	if (bPlaying && ActiveEntry.Category.IsValid() && ActiveEntry.Category.MatchesTag(Category))
	{
		if (IAudio_AudioController* Controller = ResolveAudioController())
		{
			Controller->Execute_StopCategory(Controller->_getUObject(), ActiveEntry.Category);
		}
		FinishActive();
		TryStartNext();
	}
}

// =====================================================================================================
// Queue mechanics
// =====================================================================================================

void UAudio_VOSubsystem::EnqueueOrdered(FAudio_VOQueueEntry&& Entry)
{
	// Insert keeping the array sorted by priority desc, then sequence asc, so index 0 is "play next".
	int32 InsertAt = Queue.Num();
	for (int32 Index = 0; Index < Queue.Num(); ++Index)
	{
		const FAudio_VOQueueEntry& Existing = Queue[Index];
		if (Entry.Priority > Existing.Priority ||
			(Entry.Priority == Existing.Priority && Entry.Sequence < Existing.Sequence))
		{
			InsertAt = Index;
			break;
		}
	}
	Queue.Insert(MoveTemp(Entry), InsertAt);
}

void UAudio_VOSubsystem::TryStartNext()
{
	if (bPlaying || Queue.Num() == 0)
	{
		return;
	}
	FAudio_VOQueueEntry Next = Queue[0];
	Queue.RemoveAt(0);
	StartEntry(Next);
}

void UAudio_VOSubsystem::StartEntry(const FAudio_VOQueueEntry& Entry)
{
	const FAudio_VOEntry* BankEntry = ResolveEntry(Entry.Request.LineTag);
	if (!BankEntry || BankEntry->Sound.IsNull())
	{
		// Cannot play; immediately advance so the queue never stalls.
		return;
	}

	ActiveEntry = Entry;
	bPlaying = true;

	// Determine duration up-front to schedule queue advance. Sync-load the VO sound (a line is a
	// deliberate, infrequent event; the unloaded cost was zero until now).
	USoundBase* Sound = BankEntry->Sound.LoadSynchronous();
	const UAudio_DeveloperSettings* Settings = UAudio_DeveloperSettings::Get();
	const float Fallback = Settings ? Settings->FallbackVODurationSeconds : 3.f;
	float Duration = Fallback;
	if (Sound)
	{
		const float SoundDur = Sound->GetDuration();
		// GetDuration returns a huge sentinel (INDEFINITELY_LOOPING_DURATION) for loops; clamp to fallback.
		Duration = (SoundDur > 0.f && SoundDur < 1.0e6f) ? SoundDur : Fallback;
	}
	ActiveFinishTime = NowSeconds() + Duration;

	// Push the line's duck bus (if any) so VO ducks music/SFX for its duration.
	if (Entry.DuckBusTag.IsValid())
	{
		if (UAudio_SoundManagerSubsystem* Manager = FDP_SubsystemStatics::GetGameInstanceSubsystem<UAudio_SoundManagerSubsystem>(this))
		{
			ActiveDuckHandle = Manager->PushDuckBus(Entry.DuckBusTag);
		}
	}

	// WRAP playback through the controller seam — never re-implement voice spawning.
	if (ResolveAudioController() != nullptr)
	{
		UObject* ControllerObj = AudioController.GetObject();
		if (Entry.Request.bAtLocation)
		{
			IAudio_AudioController::Execute_PlaySoundAtLocation(ControllerObj, Entry.Request.LineTag, Entry.Request.WorldLocation, Entry.Request.VolumeMult);
		}
		else
		{
			IAudio_AudioController::Execute_PlaySound2D(ControllerObj, Entry.Request.LineTag, Entry.Request.VolumeMult);
		}
	}

	// Caption forwarding: audio is caption-agnostic, but if the producer attached an opaque caption
	// payload we re-broadcast it on the well-known voice-line channel so the shipped subtitle system
	// surfaces it. We never build or inspect the payload.
	if (Entry.Request.CaptionPayload.IsValid())
	{
		if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
		{
			Bus->BroadcastPayload(AudioNativeTags::Bus_LocVoiceLine, Entry.Request.CaptionPayload, this);
		}
	}

	UE_LOG(LogDP, Verbose, TEXT("VO: playing '%s' (priority %d, ~%.2fs)."),
		*Entry.Request.LineTag.ToString(), Entry.Priority, Duration);
}

void UAudio_VOSubsystem::FinishActive()
{
	if (ActiveDuckHandle.IsValid())
	{
		if (UAudio_SoundManagerSubsystem* Manager = FDP_SubsystemStatics::GetGameInstanceSubsystem<UAudio_SoundManagerSubsystem>(this))
		{
			Manager->ReleaseDuck(ActiveDuckHandle);
		}
		ActiveDuckHandle.Invalidate();
	}
	ActiveEntry = FAudio_VOQueueEntry();
	bPlaying = false;
	ActiveFinishTime = 0.0;
}

bool UAudio_VOSubsystem::Tick(float /*DeltaTime*/)
{
	if (bPlaying && NowSeconds() >= ActiveFinishTime)
	{
		FinishActive();
		TryStartNext();
	}
	return true; // keep ticking
}

double UAudio_VOSubsystem::NowSeconds() const
{
	if (const UWorld* World = GetWorld())
	{
		// Real-time seconds: VO scheduling is cosmetic and should not stall under sim time-dilation/pause.
		return World->GetRealTimeSeconds();
	}
	return FPlatformTime::Seconds();
}

// =====================================================================================================
// Debug
// =====================================================================================================

FString UAudio_VOSubsystem::GetDPDebugString_Implementation() const
{
	return FString::Printf(TEXT("VO[device=%s banks=%d playing=%s queue=%d duck=%s]"),
		bAudioAvailable ? TEXT("yes") : TEXT("no"),
		LoadedBanks.Num(),
		bPlaying ? *ActiveEntry.Request.LineTag.ToString() : TEXT("none"),
		Queue.Num(),
		ActiveDuckHandle.IsValid() ? TEXT("yes") : TEXT("no"));
}
