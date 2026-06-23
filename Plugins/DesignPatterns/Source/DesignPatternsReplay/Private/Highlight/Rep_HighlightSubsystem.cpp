// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Highlight/Rep_HighlightSubsystem.h"
#include "Highlight/Rep_HighlightDetector.h"
#include "Highlight/Rep_HighlightRuleSet.h"
#include "Highlight/Rep_ClipController.h"
#include "Replay/Rep_ReplaySubsystem.h"
#include "Timeline/Rep_ReplayTimeline.h"
#include "Timeline/Rep_ReplayEvent.h"
#include "Settings/Rep_DeveloperSettings.h"
#include "DesignPatternsReplayModule.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Analytics/Seam_AnalyticsSink.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"

void URep_HighlightSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (const URep_DeveloperSettings* Settings = URep_DeveloperSettings::Get())
	{
		bEnableVerboseLogging = Settings->bVerboseLogging;
	}

	// Owned subobjects: created with this subsystem as outer, GC-owned via the UPROPERTY refs.
	Detector = NewObject<URep_HighlightDetector>(this, TEXT("HighlightDetector"));
	if (Detector)
	{
		Detector->OnHighlightDetected.AddDynamic(this, &URep_HighlightSubsystem::HandleHighlightDetected);
	}

	ClipController = NewObject<URep_ClipController>(this, TEXT("ClipController"));

	UE_LOG(LogDP, Log, TEXT("URep_HighlightSubsystem initialized."));
}

void URep_HighlightSubsystem::Deinitialize()
{
	// Unregister from the replay subsystem and the locator, and unbind the detector delegate.
	EndDetection();

	if (Detector)
	{
		Detector->OnHighlightDetected.RemoveDynamic(this, &URep_HighlightSubsystem::HandleHighlightDetected);
		Detector->Shutdown();
		Detector = nullptr;
	}

	if (ClipController)
	{
		ClipController->StopClip();
		ClipController = nullptr;
	}

	RuleSet = nullptr;
	ReplaySubsystem.Reset();
	AnalyticsSink.Reset();

	Super::Deinitialize();
}

// ---------------------------------------------------------------------------------------------
// Detection control
// ---------------------------------------------------------------------------------------------

void URep_HighlightSubsystem::BeginDetection()
{
	const URep_DeveloperSettings* Settings = URep_DeveloperSettings::Get();
	if (Settings && !Settings->bEnableHighlightDetection)
	{
		UE_LOG(LogDP, Verbose, TEXT("Highlights: detection disabled in settings; BeginDetection ignored."));
		return;
	}

	URep_ReplaySubsystem* Replay = ResolveReplaySubsystem();
	if (!Replay)
	{
		UE_LOG(LogDP, Warning, TEXT("Highlights: no replay subsystem; cannot begin detection."));
		return;
	}

	// Load the rule-set from the soft ref. A synchronous load is acceptable here: detection is opted
	// into around a recording/playback transition, not in a hot path. Null rule-set => no detection.
	if (!RuleSet && Settings)
	{
		RuleSet = Settings->HighlightRuleSet.LoadSynchronous();
	}
	if (!RuleSet)
	{
		UE_LOG(LogDP, Warning, TEXT("Highlights: no rule-set configured; detection inert."));
		// Still register so the timeline can poll (we simply gather nothing) — keeps wiring consistent.
	}

	if (Detector)
	{
		Detector->Initialize(Replay->GetTimeline(), RuleSet);
		// If a sidecar was already loaded for playback, sweep its existing events once.
		Detector->SweepExistingEvents();
	}

	if (!bDetecting)
	{
		Replay->RegisterEventSource(this);

		// Also publish ourselves under the highlights service key (WeakObserved — a GI subsystem the
		// engine owns; never let the locator strong-hold a cross-world-capable object).
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UDP_ServiceLocatorSubsystem* Locator = GI->GetSubsystem<UDP_ServiceLocatorSubsystem>())
			{
				Locator->RegisterService(Rep_NativeTags::Service_Replay_Highlights, this,
					EDP_ServiceLifetime::WeakObserved, /*bAllowOverride*/ true);
			}
		}
		bDetecting = true;
	}

	UE_LOG(LogDP, Verbose, TEXT("Highlights: detection begun (rules=%s)."),
		RuleSet ? TEXT("yes") : TEXT("none"));
}

void URep_HighlightSubsystem::EndDetection()
{
	if (!bDetecting)
	{
		return;
	}

	if (URep_ReplaySubsystem* Replay = ReplaySubsystem.Get())
	{
		Replay->UnregisterEventSource(this);
	}

	if (UGameInstance* GI = GetGameInstance())
	{
		if (UDP_ServiceLocatorSubsystem* Locator = GI->GetSubsystem<UDP_ServiceLocatorSubsystem>())
		{
			// Only drop the binding if it is still us (do not stomp a project override).
			if (Locator->ResolveService(Rep_NativeTags::Service_Replay_Highlights) == this)
			{
				Locator->UnregisterService(Rep_NativeTags::Service_Replay_Highlights);
			}
		}
	}

	if (Detector)
	{
		Detector->Shutdown();
	}

	bDetecting = false;
}

void URep_HighlightSubsystem::ClearHighlights()
{
	if (Detector)
	{
		Detector->Reset();
	}
	OnHighlightsChanged.Broadcast();
}

// ---------------------------------------------------------------------------------------------
// Query
// ---------------------------------------------------------------------------------------------

TArray<FRep_HighlightMoment> URep_HighlightSubsystem::GetHighlights() const
{
	return Detector ? Detector->GetMoments() : TArray<FRep_HighlightMoment>();
}

int32 URep_HighlightSubsystem::GetHighlightCount() const
{
	return Detector ? Detector->GetMomentCount() : 0;
}

FRep_HighlightReel URep_HighlightSubsystem::BuildReel(const FString& ReplayName, const FText& Title) const
{
	FRep_HighlightReel Reel;
	Reel.ReplayName = ReplayName;
	Reel.Title = Title;

	if (Detector)
	{
		Reel.Moments = Detector->GetMoments();
		// Reel order = highest score first (operator< on the moment sorts by score desc).
		Reel.Moments.Sort();
	}
	return Reel;
}

// ---------------------------------------------------------------------------------------------
// Clip playback
// ---------------------------------------------------------------------------------------------

bool URep_HighlightSubsystem::PlayHighlight(const FSeam_EntityId& MomentId, URep_PlaybackController* PlaybackController)
{
	if (!Detector || !ClipController)
	{
		return false;
	}

	FRep_HighlightMoment Moment;
	if (!Detector->FindMoment(MomentId, Moment))
	{
		UE_LOG(LogDP, Warning, TEXT("Highlights: PlayHighlight given unknown moment id."));
		return false;
	}

	ClipController->BindPlayback(PlaybackController);
	ClipController->PlayClip(Moment);
	return ClipController->IsPlayingClip();
}

// ---------------------------------------------------------------------------------------------
// Detection callback + analytics forwarding
// ---------------------------------------------------------------------------------------------

void URep_HighlightSubsystem::HandleHighlightDetected(const FRep_HighlightMoment& Moment)
{
	// Enforce the retain cap so a long session cannot grow the reel unbounded.
	if (Detector)
	{
		const URep_DeveloperSettings* Settings = URep_DeveloperSettings::Get();
		const int32 MaxRetained = Settings ? Settings->MaxRetainedHighlights : 0;
		Detector->EnforceRetainCap(MaxRetained);
	}

	ForwardToAnalytics(Moment);

	// Mirror the detect on the bus for any local listener (HUD toast, killcam director).
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UDP_MessageBusSubsystem* Bus = GI->GetSubsystem<UDP_MessageBusSubsystem>())
		{
			Bus->BroadcastPayload(Rep_NativeTags::Bus_Replay_HighlightDetected, FInstancedStruct(), this);
		}
	}

	OnHighlightsChanged.Broadcast();
}

void URep_HighlightSubsystem::ForwardToAnalytics(const FRep_HighlightMoment& Moment)
{
	const URep_DeveloperSettings* Settings = URep_DeveloperSettings::Get();
	if (!Settings || !Settings->bForwardHighlightsToAnalytics)
	{
		return;
	}

	ISeam_AnalyticsSink* Sink = ResolveAnalyticsSink();
	if (!Sink)
	{
		return;
	}

	UObject* SinkObj = AnalyticsSink.GetObject();
	if (!SinkObj || !ISeam_AnalyticsSink::Execute_IsSinkReady(SinkObj))
	{
		return;
	}

	// PII-safe aggregate: kind tag, score, contributing event count. No identity, no free-form id.
	TArray<FSeam_AnalyticsAttr> Attrs;
	Attrs.Emplace(FName(TEXT("Kind")), FSeam_NetValue::MakeTag(Moment.KindTag));
	Attrs.Emplace(FName(TEXT("Score")), FSeam_NetValue::MakeFloat(Moment.Score));
	Attrs.Emplace(FName(TEXT("Events")), FSeam_NetValue::MakeInt(Moment.ContributingEventCount));

	ISeam_AnalyticsSink::Execute_RecordAggregateEvent(SinkObj, Rep_NativeTags::Analytics_HighlightDetected, Attrs);
}

// ---------------------------------------------------------------------------------------------
// IRep_ReplayEventSource
// ---------------------------------------------------------------------------------------------

void URep_HighlightSubsystem::GatherReplayEvents_Implementation(float /*RecordingTimeSeconds*/, TArray<FRep_ReplayEvent>& OutEvents) const
{
	// Already-known highlight markers (e.g. ones detected during a prior playback sweep) are surfaced
	// as Rep.Highlight.* timeline events so the scrubber shows them when recording begins. We never
	// clear or reorder OutEvents — only append (per the seam contract).
	if (!Detector)
	{
		return;
	}
	for (const FRep_HighlightMoment& Moment : Detector->GetMoments())
	{
		OutEvents.Emplace(Moment.AnchorTimeSeconds, Moment.KindTag, Moment.DisplayLabel, Moment.Magnitude);
	}
}

FGameplayTag URep_HighlightSubsystem::GetEventSourceId_Implementation() const
{
	return Rep_NativeTags::Service_Replay_Highlights;
}

// ---------------------------------------------------------------------------------------------
// FTickableGameObject
// ---------------------------------------------------------------------------------------------

void URep_HighlightSubsystem::Tick(float DeltaTime)
{
	if (ClipController && ClipController->IsPlayingClip())
	{
		ClipController->Tick(DeltaTime);
	}
}

TStatId URep_HighlightSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(URep_HighlightSubsystem, STATGROUP_Tickables);
}

bool URep_HighlightSubsystem::IsTickable() const
{
	// Conditional tick: only run while a clip is actively playing (inert otherwise).
	return ClipController != nullptr && ClipController->IsPlayingClip();
}

UWorld* URep_HighlightSubsystem::GetTickableGameObjectWorld() const
{
	// Tie ticking to the game instance's current world so we never tick a CDO/editor context.
	if (const UGameInstance* GI = GetGameInstance())
	{
		return GI->GetWorld();
	}
	return nullptr;
}

// ---------------------------------------------------------------------------------------------
// Resolution helpers + debug
// ---------------------------------------------------------------------------------------------

URep_ReplaySubsystem* URep_HighlightSubsystem::ResolveReplaySubsystem()
{
	if (URep_ReplaySubsystem* Cached = ReplaySubsystem.Get())
	{
		return Cached;
	}
	if (UGameInstance* GI = GetGameInstance())
	{
		if (URep_ReplaySubsystem* Found = GI->GetSubsystem<URep_ReplaySubsystem>())
		{
			ReplaySubsystem = Found;
			return Found;
		}
	}
	return nullptr;
}

ISeam_AnalyticsSink* URep_HighlightSubsystem::ResolveAnalyticsSink()
{
	// Return the cached weak interface if still live (pruned-on-use).
	if (AnalyticsSink.IsValid())
	{
		return AnalyticsSink.Get();
	}
	AnalyticsSink.Reset();

	const URep_DeveloperSettings* Settings = URep_DeveloperSettings::Get();
	if (!Settings || !Settings->AnalyticsSinkServiceTag.IsValid())
	{
		return nullptr;
	}

	UGameInstance* GI = GetGameInstance();
	if (!GI)
	{
		return nullptr;
	}
	UDP_ServiceLocatorSubsystem* Locator = GI->GetSubsystem<UDP_ServiceLocatorSubsystem>();
	if (!Locator)
	{
		return nullptr;
	}

	UObject* Provider = Locator->ResolveService(Settings->AnalyticsSinkServiceTag);
	if (Provider && Provider->GetClass()->ImplementsInterface(USeam_AnalyticsSink::StaticClass()))
	{
		// TWeakInterfacePtr's UObject* constructor performs the interface cast and stores both the
		// object and interface pointers; pruned-on-use via IsValid()/Get().
		AnalyticsSink = TWeakInterfacePtr<ISeam_AnalyticsSink>(Provider);
		if (AnalyticsSink.IsValid())
		{
			return AnalyticsSink.Get();
		}
	}
	return nullptr;
}

FString URep_HighlightSubsystem::GetDPDebugString_Implementation() const
{
	const int32 Count = GetHighlightCount();
	const bool bClip = ClipController && ClipController->IsPlayingClip();
	return FString::Printf(TEXT("Highlights[%s] moments=%d clip=%s"),
		bDetecting ? TEXT("on") : TEXT("off"), Count, bClip ? TEXT("playing") : TEXT("idle"));
}
