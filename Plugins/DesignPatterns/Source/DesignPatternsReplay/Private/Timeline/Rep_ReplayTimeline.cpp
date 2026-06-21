// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Timeline/Rep_ReplayTimeline.h"
#include "Replay/Rep_ReplaySubsystem.h"
#include "Settings/Rep_DeveloperSettings.h"
#include "DesignPatternsReplayModule.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Core/DPLog.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/BufferArchive.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"

/** Magic + version stamped at the head of a sidecar so we can reject/upgrade old files safely. */
namespace Rep_SidecarPrivate
{
	static const uint32 SidecarMagic = 0x52455054; // 'REPT'
	static const int32  SidecarVersion = 1;
}

void URep_ReplayTimeline::InitializeForSubsystem(URep_ReplaySubsystem* InOwner)
{
	OwnerSubsystem = InOwner;
}

void URep_ReplayTimeline::Shutdown()
{
	if (bRecording)
	{
		EndRecording();
	}
	ClearTimeline();
	OwnerSubsystem = nullptr;
}

// ---------------------------------------------------------------------------------------------
// Recording
// ---------------------------------------------------------------------------------------------

void URep_ReplayTimeline::BeginRecording(const FString& ReplayName)
{
	// Fresh recording: drop any prior (playback) events.
	Events.Reset();
	OnTimelineReset.Broadcast();

	bRecording = true;
	RecordingName = ReplayName;
	RecordingStartSeconds = FPlatformTime::Seconds();
	MaxEvents = 0;

	bool bAutoRecord = true;
	FGameplayTag BusRoot = Rep_NativeTags::Bus_Replay;
	if (const URep_DeveloperSettings* Settings = URep_DeveloperSettings::Get())
	{
		bAutoRecord = Settings->bAutoRecordTimeline;
		MaxEvents = Settings->MaxTimelineEvents;
		if (Settings->TimelineBusRoot.IsValid())
		{
			BusRoot = Settings->TimelineBusRoot;
		}
	}

	SubscribedBusRoot = BusRoot;

	if (bAutoRecord)
	{
		if (UDP_MessageBusSubsystem* Bus = GetMessageBus())
		{
			// Listen under the configured root (child-matching) so games opt events in by broadcasting there.
			TWeakObjectPtr<URep_ReplayTimeline> WeakThis(this);
			BusListenerHandle = Bus->ListenNative(
				BusRoot,
				[WeakThis](const FDP_Message& Message)
				{
					if (URep_ReplayTimeline* Self = WeakThis.Get())
					{
						Self->HandleBusMessage(Message);
					}
				},
				this,
				EDP_MessageMatch::ExactOrChild);
		}
	}

	// Poll registered curated event sources for already-known markers at t=0.
	if (URep_ReplaySubsystem* Owner = OwnerSubsystem.Get())
	{
		TArray<FRep_ReplayEvent> Gathered;
		Owner->GatherFromEventSources(0.f, Gathered);
		for (const FRep_ReplayEvent& E : Gathered)
		{
			RecordEvent(E, /*bUseProvidedTime*/ true);
		}
	}

	UE_LOG(LogDP, Verbose, TEXT("Replay timeline: begin recording '%s' (autoRecord=%d, cap=%d)."),
		*ReplayName, bAutoRecord ? 1 : 0, MaxEvents);
}

void URep_ReplayTimeline::EndRecording()
{
	if (!bRecording)
	{
		return;
	}

	// Detach the bus listener.
	if (UDP_MessageBusSubsystem* Bus = GetMessageBus())
	{
		Bus->StopListeningForOwner(this);
	}
	BusListenerHandle = FDP_ListenerHandle();

	bRecording = false;

	// Flush the sidecar so the timeline travels with the demo.
	const FString Path = GetSidecarPath(RecordingName);
	const FString Dir = FPaths::GetPath(Path);
	IFileManager::Get().MakeDirectory(*Dir, /*Tree*/ true);

	FBufferArchive Buffer;
	uint32 Magic = Rep_SidecarPrivate::SidecarMagic;
	int32 Version = Rep_SidecarPrivate::SidecarVersion;
	Buffer << Magic;
	Buffer << Version;

	int32 Count = Events.Num();
	Buffer << Count;
	for (FRep_ReplayEvent& E : Events)
	{
		Buffer << E.Time;

		FString TagString = E.EventTag.IsValid() ? E.EventTag.ToString() : FString();
		Buffer << TagString;

		FString Label = E.DisplayLabel.ToString();
		Buffer << Label;

		// Payload via the FSeam_NetValue net serializer (compact, closed-variant, no reflection edge cases).
		bool bOk = true;
		E.Payload.NetSerialize(Buffer, /*Map*/ nullptr, bOk);
	}

	if (FFileHelper::SaveArrayToFile(Buffer, *Path))
	{
		UE_LOG(LogDP, Log, TEXT("Replay timeline: flushed %d events to '%s'."), Events.Num(), *Path);
	}
	else
	{
		UE_LOG(LogDP, Warning, TEXT("Replay timeline: failed to write sidecar '%s'."), *Path);
	}

	RecordingName.Reset();
}

void URep_ReplayTimeline::RecordEvent(const FRep_ReplayEvent& Event, bool bUseProvidedTime)
{
	if (!bRecording)
	{
		UE_LOG(LogDP, Warning, TEXT("Replay timeline: RecordEvent called while not recording; ignored."));
		return;
	}
	if (!Event.IsValid())
	{
		return;
	}

	FRep_ReplayEvent Stamped = Event;
	if (!bUseProvidedTime)
	{
		Stamped.Time = CurrentRecordingTime();
	}
	InsertSorted(Stamped);
	OnTimelineEventRecorded.Broadcast(Stamped);
}

void URep_ReplayTimeline::AddBookmark(const FText& Label)
{
	FRep_ReplayEvent E;
	E.EventTag = Rep_NativeTags::Event_Bookmark;
	E.DisplayLabel = Label;
	RecordEvent(E, /*bUseProvidedTime*/ false);
}

// ---------------------------------------------------------------------------------------------
// Playback
// ---------------------------------------------------------------------------------------------

bool URep_ReplayTimeline::LoadForPlayback(const FString& ReplayName)
{
	ClearTimeline();

	const FString Path = GetSidecarPath(ReplayName);
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *Path))
	{
		// No sidecar: the scrubber shows a marker-less timeline (documented inert default).
		UE_LOG(LogDP, Verbose, TEXT("Replay timeline: no sidecar for '%s'; showing marker-less timeline."), *ReplayName);
		return false;
	}

	FMemoryReader Reader(Bytes, /*bIsPersistent*/ true);
	uint32 Magic = 0;
	int32 Version = 0;
	Reader << Magic;
	Reader << Version;
	if (Magic != Rep_SidecarPrivate::SidecarMagic)
	{
		UE_LOG(LogDP, Warning, TEXT("Replay timeline: sidecar '%s' has a bad magic; ignoring."), *Path);
		return false;
	}

	int32 Count = 0;
	Reader << Count;
	Events.Reserve(Count);
	for (int32 i = 0; i < Count && !Reader.IsError(); ++i)
	{
		FRep_ReplayEvent E;
		Reader << E.Time;

		FString TagString;
		Reader << TagString;
		if (!TagString.IsEmpty())
		{
			E.EventTag = FGameplayTag::RequestGameplayTag(FName(*TagString), /*ErrorIfNotFound*/ false);
		}

		FString Label;
		Reader << Label;
		E.DisplayLabel = FText::FromString(Label);

		bool bOk = true;
		E.Payload.NetSerialize(Reader, /*Map*/ nullptr, bOk);

		Events.Add(MoveTemp(E));
	}

	Events.Sort();
	OnTimelineReset.Broadcast();
	UE_LOG(LogDP, Log, TEXT("Replay timeline: loaded %d events for '%s'."), Events.Num(), *ReplayName);
	return Events.Num() > 0;
}

void URep_ReplayTimeline::ClearTimeline()
{
	if (Events.Num() > 0)
	{
		Events.Reset();
		OnTimelineReset.Broadcast();
	}
}

// ---------------------------------------------------------------------------------------------
// Query
// ---------------------------------------------------------------------------------------------

bool URep_ReplayTimeline::FindNextEvent(float FromTime, FGameplayTag FilterTag, FRep_ReplayEvent& OutEvent) const
{
	for (const FRep_ReplayEvent& E : Events)
	{
		if (E.Time < FromTime)
		{
			continue;
		}
		if (FilterTag.IsValid() && !E.EventTag.MatchesTag(FilterTag))
		{
			continue;
		}
		OutEvent = E;
		return true;
	}
	return false;
}

bool URep_ReplayTimeline::FindPreviousEvent(float FromTime, FGameplayTag FilterTag, FRep_ReplayEvent& OutEvent) const
{
	bool bFound = false;
	for (const FRep_ReplayEvent& E : Events)
	{
		if (E.Time > FromTime)
		{
			break; // Events is sorted ascending
		}
		if (FilterTag.IsValid() && !E.EventTag.MatchesTag(FilterTag))
		{
			continue;
		}
		OutEvent = E;
		bFound = true;
	}
	return bFound;
}

FString URep_ReplayTimeline::GetSidecarPath(const FString& ReplayName)
{
	// Sidecars live next to the project's demos so they travel with them.
	const FString SafeName = FPaths::MakeValidFileName(ReplayName);
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Demos"), TEXT("Timelines"),
		SafeName + TEXT(".dptimeline"));
}

// ---------------------------------------------------------------------------------------------
// Internals
// ---------------------------------------------------------------------------------------------

float URep_ReplayTimeline::CurrentRecordingTime() const
{
	return (float)(FPlatformTime::Seconds() - RecordingStartSeconds);
}

void URep_ReplayTimeline::InsertSorted(const FRep_ReplayEvent& Event)
{
	// Events almost always arrive in time order while recording, so a tail insert + bubble is cheap.
	int32 Index = Events.Num();
	while (Index > 0 && Event.Time < Events[Index - 1].Time)
	{
		--Index;
	}
	Events.Insert(Event, Index);
	EnforceCap();
}

void URep_ReplayTimeline::HandleBusMessage(const FDP_Message& Message)
{
	if (!bRecording)
	{
		return;
	}

	FRep_ReplayEvent E;
	E.Time = CurrentRecordingTime();
	// Tag the timeline event as a promoted bus message but remember the source channel as the label.
	E.EventTag = Rep_NativeTags::Event_BusMessage;
	E.DisplayLabel = FText::FromString(Message.Channel.ToString());

	InsertSorted(E);
	OnTimelineEventRecorded.Broadcast(E);
}

UDP_MessageBusSubsystem* URep_ReplayTimeline::GetMessageBus() const
{
	if (const URep_ReplaySubsystem* Owner = OwnerSubsystem.Get())
	{
		if (UGameInstance* GI = Owner->GetGameInstance())
		{
			return GI->GetSubsystem<UDP_MessageBusSubsystem>();
		}
	}
	return nullptr;
}

void URep_ReplayTimeline::EnforceCap()
{
	if (MaxEvents <= 0 || Events.Num() <= MaxEvents)
	{
		return;
	}

	// Over cap: drop oldest NON-bookmark events first (bookmarks are user-meaningful and kept).
	int32 ToDrop = Events.Num() - MaxEvents;
	for (int32 i = 0; i < Events.Num() && ToDrop > 0; )
	{
		if (Events[i].EventTag != Rep_NativeTags::Event_Bookmark)
		{
			Events.RemoveAt(i);
			--ToDrop;
		}
		else
		{
			++i;
		}
	}
	// If still over cap (all bookmarks), drop oldest regardless.
	while (Events.Num() > MaxEvents && Events.Num() > 0)
	{
		Events.RemoveAt(0);
	}
}
