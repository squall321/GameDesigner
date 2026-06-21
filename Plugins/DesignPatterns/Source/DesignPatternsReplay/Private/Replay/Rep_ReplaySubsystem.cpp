// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Replay/Rep_ReplaySubsystem.h"
#include "Timeline/Rep_ReplayTimeline.h"
#include "Timeline/Rep_ReplayEvent.h"
#include "Seam/Rep_ReplayEventSource.h"
#include "Settings/Rep_DeveloperSettings.h"
#include "DesignPatternsReplayModule.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Core/DPLog.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/DemoNetDriver.h"
#include "NetworkReplayStreaming.h"
#include "HAL/FileManager.h"

void URep_ReplaySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (const URep_DeveloperSettings* Settings = URep_DeveloperSettings::Get())
	{
		bEnableVerboseLogging = Settings->bVerboseLogging;
	}

	// Instanced subobject created with this subsystem as outer (GC-owned via the UPROPERTY below).
	Timeline = NewObject<URep_ReplayTimeline>(this, TEXT("ReplayTimeline"));
	if (Timeline)
	{
		Timeline->InitializeForSubsystem(this);
	}

	UE_LOG(LogDP, Log, TEXT("URep_ReplaySubsystem initialized."));
}

void URep_ReplaySubsystem::Deinitialize()
{
	// Stop any in-progress recording cleanly so the timeline sidecar is flushed.
	if (IsRecording())
	{
		StopRecording();
	}

	if (Timeline)
	{
		Timeline->Shutdown();
		Timeline = nullptr;
	}

	EventSources.Reset();
	Replays.Reset();

	Super::Deinitialize();
}

// ---------------------------------------------------------------------------------------------
// Recording
// ---------------------------------------------------------------------------------------------

FString URep_ReplaySubsystem::StartRecording(const FString& Name, const FText& FriendlyName)
{
	UGameInstance* GI = GetGameInstance();
	if (!GI)
	{
		UE_LOG(LogDP, Warning, TEXT("Replay: StartRecording with no GameInstance."));
		return FString();
	}

	if (IsRecording())
	{
		UE_LOG(LogDP, Warning, TEXT("Replay: already recording '%s'; ignoring StartRecording."), *ActiveRecordingName);
		return FString();
	}

	if (IsPlaying())
	{
		UE_LOG(LogDP, Warning, TEXT("Replay: cannot record while a replay is playing."));
		return FString();
	}

	// MULTIPLAYER CAVEAT: only an authority (server/standalone) can record the authoritative demo.
	if (!CanRecordHere())
	{
		UE_LOG(LogDP, Warning, TEXT("Replay: StartRecording rejected on a pure client; record on the server."));
		return FString();
	}

	const FString ResolvedName = Name.IsEmpty() ? BuildAutoReplayName() : Name;
	const FText ResolvedFriendly = BuildFriendlyName(FriendlyName);

	// WRAP the engine: this is the entire recording mechanism — no custom net code.
	const TArray<FString> AdditionalOptions;
	GI->StartRecordingReplay(ResolvedName, ResolvedFriendly.ToString(), AdditionalOptions);

	ActiveRecordingName = ResolvedName;

	// Begin harvesting the gameplay-event timeline alongside the demo.
	if (Timeline)
	{
		Timeline->BeginRecording(ResolvedName);
	}

	// Notify listeners (bus + delegate).
	if (UDP_MessageBusSubsystem* Bus = GI->GetSubsystem<UDP_MessageBusSubsystem>())
	{
		Bus->BroadcastPayload(Rep_NativeTags::Bus_Replay_RecordingStarted, FInstancedStruct(), this);
	}
	OnReplayStateChanged.Broadcast(ResolvedName);

	UE_LOG(LogDP, Log, TEXT("Replay: started recording '%s' (%s)."), *ResolvedName, *ResolvedFriendly.ToString());
	return ResolvedName;
}

void URep_ReplaySubsystem::StopRecording()
{
	UGameInstance* GI = GetGameInstance();
	if (!GI || !IsRecording())
	{
		return;
	}

	const FString StoppedName = ActiveRecordingName;

	// Flush the sidecar BEFORE telling the engine to stop, so the timeline can name its file.
	if (Timeline)
	{
		Timeline->EndRecording();
	}

	GI->StopRecordingReplay();
	ActiveRecordingName.Reset();

	if (UDP_MessageBusSubsystem* Bus = GI->GetSubsystem<UDP_MessageBusSubsystem>())
	{
		Bus->BroadcastPayload(Rep_NativeTags::Bus_Replay_RecordingStopped, FInstancedStruct(), this);
	}
	OnReplayStateChanged.Broadcast(StoppedName);

	UE_LOG(LogDP, Log, TEXT("Replay: stopped recording '%s'."), *StoppedName);

	// Refresh the registry so the just-recorded demo shows up for UI.
	RefreshReplays();
}

bool URep_ReplaySubsystem::IsRecording() const
{
	const UGameInstance* GI = GetGameInstance();
	if (!GI)
	{
		return false;
	}
	// The demo net driver is present and not in playback => recording.
	if (const UWorld* World = GI->GetWorld())
	{
		if (const UDemoNetDriver* Demo = World->GetDemoNetDriver())
		{
			return Demo->IsRecording();
		}
	}
	return !ActiveRecordingName.IsEmpty();
}

// ---------------------------------------------------------------------------------------------
// Playback
// ---------------------------------------------------------------------------------------------

bool URep_ReplaySubsystem::PlayReplay(const FString& Name)
{
	UGameInstance* GI = GetGameInstance();
	if (!GI)
	{
		UE_LOG(LogDP, Warning, TEXT("Replay: PlayReplay with no GameInstance."));
		return false;
	}

	if (Name.IsEmpty())
	{
		UE_LOG(LogDP, Warning, TEXT("Replay: PlayReplay called with an empty name."));
		return false;
	}

	if (IsRecording())
	{
		UE_LOG(LogDP, Warning, TEXT("Replay: stop recording before starting playback."));
		return false;
	}

	// WRAP the engine: PlayReplay spins up a client-only UDemoNetDriver in this game instance.
	const TArray<FString> AdditionalOptions;
	const bool bStarted = GI->PlayReplay(Name, /*WorldOverride*/ nullptr, AdditionalOptions);
	if (!bStarted)
	{
		UE_LOG(LogDP, Warning, TEXT("Replay: engine PlayReplay('%s') returned false."), *Name);
		return false;
	}

	// Load the sidecar timeline for the scrubber (degrades to marker-less if missing).
	if (Timeline)
	{
		Timeline->LoadForPlayback(Name);
	}

	if (UDP_MessageBusSubsystem* Bus = GI->GetSubsystem<UDP_MessageBusSubsystem>())
	{
		Bus->BroadcastPayload(Rep_NativeTags::Bus_Replay_PlaybackStarted, FInstancedStruct(), this);
	}
	OnReplayStateChanged.Broadcast(Name);

	UE_LOG(LogDP, Log, TEXT("Replay: started playback of '%s'."), *Name);
	return true;
}

bool URep_ReplaySubsystem::IsPlaying() const
{
	const UGameInstance* GI = GetGameInstance();
	if (!GI)
	{
		return false;
	}
	if (const UWorld* World = GI->GetWorld())
	{
		if (const UDemoNetDriver* Demo = World->GetDemoNetDriver())
		{
			return Demo->IsPlaying();
		}
	}
	return false;
}

// ---------------------------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------------------------

TSharedPtr<INetworkReplayStreamer> URep_ReplaySubsystem::GetActiveStreamer() const
{
	// Prefer the live demo driver's streamer (so we enumerate the same backend it records to);
	// otherwise create a fresh streamer from the configured factory.
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (const UWorld* World = GI->GetWorld())
		{
			if (const UDemoNetDriver* Demo = World->GetDemoNetDriver())
			{
				if (Demo->ReplayStreamer.IsValid())
				{
					return Demo->ReplayStreamer;
				}
			}
		}
	}
	return FNetworkReplayStreaming::Get().GetFactory().CreateReplayStreamer();
}

void URep_ReplaySubsystem::RefreshReplays()
{
	if (bEnumerationInFlight)
	{
		return; // coalesce overlapping requests
	}

	TSharedPtr<INetworkReplayStreamer> Streamer = GetActiveStreamer();
	if (!Streamer.IsValid())
	{
		UE_LOG(LogDP, Warning, TEXT("Replay: no replay streamer available to enumerate."));
		Replays.Reset();
		OnReplayRegistryUpdated.Broadcast();
		return;
	}

	bEnumerationInFlight = true;

	// EnumerateStreams is async; capture a weak self so a torn-down subsystem does not crash the cb.
	TWeakObjectPtr<URep_ReplaySubsystem> WeakThis(this);

	FNetworkReplayVersion Version; // default version => all versions for this app
	FEnumerateStreamsCallback Callback = FEnumerateStreamsCallback::CreateLambda(
		[WeakThis](const FEnumerateStreamsResult& Result)
		{
			URep_ReplaySubsystem* Self = WeakThis.Get();
			if (!Self)
			{
				return;
			}
			Self->bEnumerationInFlight = false;
			Self->OnStreamsEnumerated(Result.FoundStreams);
		});

	Streamer->EnumerateStreams(Version, INDEX_NONE, FString(), TArray<FString>(), Callback);
}

void URep_ReplaySubsystem::OnStreamsEnumerated(const TArray<FNetworkReplayStreamInfo>& Streams)
{
	Replays.Reset();
	Replays.Reserve(Streams.Num());

	for (const FNetworkReplayStreamInfo& Info : Streams)
	{
		FRep_ReplayInfo Row;
		Row.Name = Info.Name;
		Row.FriendlyName = FText::FromString(Info.FriendlyName);
		Row.DurationSeconds = Info.LengthInMS / 1000.f;
		Row.Timestamp = Info.Timestamp;
		Row.SizeInBytes = Info.SizeInBytes;
		Row.bIsLive = Info.bIsLive;

		// The streamer does not carry the map; recover it (and the sidecar marker count) best-effort
		// from the timeline sidecar if present. Missing sidecar => 0 events, empty map (inert default).
		Row.TimelineEventCount = 0;

		Replays.Add(MoveTemp(Row));
	}

	// Newest first for UI convenience.
	Replays.Sort([](const FRep_ReplayInfo& A, const FRep_ReplayInfo& B)
	{
		return A.Timestamp > B.Timestamp;
	});

	UE_LOG(LogDP, Verbose, TEXT("Replay: registry refreshed (%d stored replays)."), Replays.Num());
	OnReplayRegistryUpdated.Broadcast();
}

bool URep_ReplaySubsystem::FindReplay(const FString& Name, FRep_ReplayInfo& OutInfo) const
{
	for (const FRep_ReplayInfo& Row : Replays)
	{
		if (Row.Name == Name)
		{
			OutInfo = Row;
			return true;
		}
	}
	return false;
}

void URep_ReplaySubsystem::DeleteReplay(const FString& Name)
{
	if (Name.IsEmpty())
	{
		return;
	}

	if (TSharedPtr<INetworkReplayStreamer> Streamer = GetActiveStreamer())
	{
		TWeakObjectPtr<URep_ReplaySubsystem> WeakThis(this);
		FDeleteFinishedStreamCallback Callback = FDeleteFinishedStreamCallback::CreateLambda(
			[WeakThis](const FDeleteFinishedStreamResult& /*Result*/)
			{
				if (URep_ReplaySubsystem* Self = WeakThis.Get())
				{
					Self->RefreshReplays();
				}
			});
		Streamer->DeleteFinishedStream(Name, Callback);
	}

	// Also remove the sidecar timeline file so it does not orphan.
	const FString Sidecar = URep_ReplayTimeline::GetSidecarPath(Name);
	if (IFileManager::Get().FileExists(*Sidecar))
	{
		IFileManager::Get().Delete(*Sidecar);
	}

	UE_LOG(LogDP, Log, TEXT("Replay: requested delete of '%s'."), *Name);
}

// ---------------------------------------------------------------------------------------------
// Event sources
// ---------------------------------------------------------------------------------------------

void URep_ReplaySubsystem::RegisterEventSource(const TScriptInterface<IRep_ReplayEventSource>& Source)
{
	if (!Source.GetObject())
	{
		return;
	}
	PruneEventSources();

	for (const TWeakInterfacePtr<IRep_ReplayEventSource>& Existing : EventSources)
	{
		if (Existing.GetObject() == Source.GetObject())
		{
			return; // already registered
		}
	}
	EventSources.Add(TWeakInterfacePtr<IRep_ReplayEventSource>(Source.GetInterface()));
}

void URep_ReplaySubsystem::UnregisterEventSource(const TScriptInterface<IRep_ReplayEventSource>& Source)
{
	const UObject* Obj = Source.GetObject();
	EventSources.RemoveAll([Obj](const TWeakInterfacePtr<IRep_ReplayEventSource>& E)
	{
		return !E.IsValid() || E.GetObject() == Obj;
	});
}

void URep_ReplaySubsystem::GatherFromEventSources(float RecordingTimeSeconds, TArray<FRep_ReplayEvent>& OutEvents)
{
	PruneEventSources();
	for (const TWeakInterfacePtr<IRep_ReplayEventSource>& Weak : EventSources)
	{
		if (UObject* Obj = Weak.GetObject())
		{
			IRep_ReplayEventSource::Execute_GatherReplayEvents(Obj, RecordingTimeSeconds, OutEvents);
		}
	}
}

void URep_ReplaySubsystem::PruneEventSources()
{
	EventSources.RemoveAll([](const TWeakInterfacePtr<IRep_ReplayEventSource>& E)
	{
		return !E.IsValid();
	});
}

// ---------------------------------------------------------------------------------------------
// Helpers / debug
// ---------------------------------------------------------------------------------------------

FString URep_ReplaySubsystem::BuildAutoReplayName() const
{
	FString Base = TEXT("DPReplay");
	if (const URep_DeveloperSettings* Settings = URep_DeveloperSettings::Get())
	{
		if (!Settings->DefaultReplayName.IsEmpty())
		{
			Base = Settings->DefaultReplayName;
		}
	}
	// Timestamp suffix avoids clobbering successive auto recordings.
	return FString::Printf(TEXT("%s_%s"), *Base, *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
}

FText URep_ReplaySubsystem::BuildFriendlyName(const FText& Provided) const
{
	if (!Provided.IsEmpty())
	{
		return Provided;
	}

	FString MapName;
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (const UWorld* World = GI->GetWorld())
		{
			MapName = World->GetMapName();
			MapName.RemoveFromStart(World->StreamingLevelsPrefix);
		}
	}

	FString Template = TEXT("Replay - {Map}");
	if (const URep_DeveloperSettings* Settings = URep_DeveloperSettings::Get())
	{
		if (!Settings->DefaultFriendlyNameTemplate.IsEmpty())
		{
			Template = Settings->DefaultFriendlyNameTemplate;
		}
	}
	Template.ReplaceInline(TEXT("{Map}"), *MapName);
	return FText::FromString(Template);
}

bool URep_ReplaySubsystem::CanRecordHere() const
{
	const UGameInstance* GI = GetGameInstance();
	if (!GI)
	{
		return false;
	}
	if (const UWorld* World = GI->GetWorld())
	{
		// A pure client (NM_Client) cannot record the authoritative demo.
		return World->GetNetMode() != NM_Client;
	}
	return true;
}

FString URep_ReplaySubsystem::GetDPDebugString_Implementation() const
{
	const TCHAR* State = IsRecording() ? TEXT("REC") : (IsPlaying() ? TEXT("PLAY") : TEXT("idle"));
	const int32 TimelineEvents = (Timeline ? Timeline->GetEventCount() : 0);
	return FString::Printf(TEXT("Replay[%s] active='%s' stored=%d timeline=%d sources=%d"),
		State, *ActiveRecordingName, Replays.Num(), TimelineEvents, EventSources.Num());
}
