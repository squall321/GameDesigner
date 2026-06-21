// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "MessageBus/DPMessage.h"
#include "Timeline/Rep_ReplayEvent.h"
#include "Rep_ReplayTimeline.generated.h"

class URep_ReplaySubsystem;
class UDP_MessageBusSubsystem;

/** Fired whenever an event is appended to the timeline (so a live scrubber can add a marker). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRep_OnTimelineEventRecorded, const FRep_ReplayEvent&, Event);
/** Fired when the timeline is loaded/cleared and the whole marker set should be re-read. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRep_OnTimelineReset);

/**
 * URep_ReplayTimeline — records significant gameplay events alongside a demo so a scrubber can
 * show markers and jump to them.
 *
 * The engine demo stream contains replicated actor state but NOT a curated list of "moments".
 * This object builds that list. While recording it:
 *  - Subscribes to the message bus under the configured root (DP.Bus.Replay by default) and turns
 *    each broadcast into a Rep.Event.BusMessage timeline event.
 *  - Snapshots the core command-history replay stream count to emit Rep.Event.Command markers.
 *  - Polls registered IRep_ReplayEventSource contributors for curated markers.
 * Each event's Time is recording-relative (seconds since recording began), so it lines up with the
 * demo's own timeline on playback.
 *
 * Storage: the event list is kept in memory during recording and FLUSHED to a sidecar save file
 * named after the demo (so it travels with the demo without needing the engine to embed custom
 * data in the net stream). On playback it is LOADED from that sidecar and exposed read-only for the
 * scrubber. This is the "stored alongside the demo via a sidecar" strategy — no net format changes.
 *
 * Owned by URep_ReplaySubsystem (a UPROPERTY TObjectPtr there); never standalone.
 */
UCLASS()
class DESIGNPATTERNSREPLAY_API URep_ReplayTimeline : public UObject
{
	GENERATED_BODY()

public:
	/** Wire this timeline to its owning subsystem. Called once by the subsystem at Initialize. */
	void InitializeForSubsystem(URep_ReplaySubsystem* InOwner);

	/** Detach bus listeners and clear transient state. Called by the subsystem at Deinitialize. */
	void Shutdown();

	// ---- Recording side ----

	/**
	 * Begin harvesting events for a demo named ReplayName. Resets the event list, records the
	 * recording start time, subscribes to the bus (if auto-record is enabled in settings) and polls
	 * registered event sources for already-known markers.
	 */
	void BeginRecording(const FString& ReplayName);

	/**
	 * Stop harvesting and flush the accumulated events to the sidecar for ReplayName. After this the
	 * timeline is idle until the next BeginRecording or LoadForPlayback.
	 */
	void EndRecording();

	/** True while actively harvesting events. */
	bool IsRecording() const { return bRecording; }

	/**
	 * Append a curated event at the CURRENT recording time. No-op (with a warning) if not recording.
	 * The Time field of the passed event is overwritten with the live recording-relative time unless
	 * bUseProvidedTime is set (used when replaying back already-timed source events).
	 */
	void RecordEvent(const FRep_ReplayEvent& Event, bool bUseProvidedTime = false);

	/** Convenience: drop a manual bookmark marker (Rep.Event.Bookmark) at the current time. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Timeline")
	void AddBookmark(const FText& Label);

	// ---- Playback side ----

	/**
	 * Load the sidecar timeline recorded for ReplayName so the scrubber can show markers during
	 * playback. Clears any current events first; broadcasts OnTimelineReset. Returns false if no
	 * sidecar exists (the scrubber then shows a marker-less timeline — a documented inert default).
	 */
	bool LoadForPlayback(const FString& ReplayName);

	/** Drop all events and broadcast a reset (used when leaving playback or starting fresh). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Timeline")
	void ClearTimeline();

	// ---- Query (for the view-model / playback controller) ----

	/** All recorded events in chronological order. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Replay|Timeline")
	const TArray<FRep_ReplayEvent>& GetEvents() const { return Events; }

	/** Number of recorded events. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Replay|Timeline")
	int32 GetEventCount() const { return Events.Num(); }

	/**
	 * The first event at or after FromTime whose tag matches FilterTag (or any event if FilterTag is
	 * invalid). Returns false if none. Used by SeekToEvent("next").
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Timeline")
	bool FindNextEvent(float FromTime, FGameplayTag FilterTag, FRep_ReplayEvent& OutEvent) const;

	/** The last event at or before FromTime matching FilterTag. Returns false if none. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Timeline")
	bool FindPreviousEvent(float FromTime, FGameplayTag FilterTag, FRep_ReplayEvent& OutEvent) const;

	// ---- Delegates ----

	/** Broadcast when a new event is appended while recording (so a live scrubber updates). */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Replay|Timeline")
	FRep_OnTimelineEventRecorded OnTimelineEventRecorded;

	/** Broadcast when the whole event set changes (load / clear). */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Replay|Timeline")
	FRep_OnTimelineReset OnTimelineReset;

	/** The sidecar file name (under the project's Saved/Replays dir) for a given replay name. */
	static FString GetSidecarPath(const FString& ReplayName);

private:
	/** Back-pointer to the owning subsystem; weak so the timeline never keeps it alive. */
	UPROPERTY()
	TWeakObjectPtr<URep_ReplaySubsystem> OwnerSubsystem;

	/** Chronological event buffer (kept sorted on insert). */
	UPROPERTY()
	TArray<FRep_ReplayEvent> Events;

	/** True while harvesting. */
	bool bRecording = false;

	/** World-seconds (real time) at which recording began — the zero of event Time. */
	double RecordingStartSeconds = 0.0;

	/** The replay name currently being recorded, used to name the sidecar at EndRecording. */
	FString RecordingName;

	/** The resolved bus root we subscribed under, cached for unsubscribe. */
	FGameplayTag SubscribedBusRoot;

	/** Cap copied from settings at BeginRecording; 0 = unbounded. */
	int32 MaxEvents = 0;

	/** Bus listener handle so we can stop listening at EndRecording. */
	FDP_ListenerHandle BusListenerHandle;

	/** Current recording-relative time in seconds (now - RecordingStartSeconds). */
	float CurrentRecordingTime() const;

	/** Insert an event keeping Events sorted by Time; enforces the MaxEvents cap. */
	void InsertSorted(const FRep_ReplayEvent& Event);

	/** Handle a bus broadcast under our subscribed root: promote it to a timeline event. */
	void HandleBusMessage(const FDP_Message& Message);

	/** Resolve the message bus subsystem for the owning game instance, or null. */
	UDP_MessageBusSubsystem* GetMessageBus() const;

	/** Trim oldest non-bookmark events down to MaxEvents (bookmarks are preserved preferentially). */
	void EnforceCap();
};
