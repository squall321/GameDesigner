// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "UObject/WeakInterfacePtr.h"
#include "Rep_ReplaySubsystem.generated.h"

class IRep_ReplayEventSource;
class URep_ReplayTimeline;
class INetworkReplayStreamer;
struct FNetworkReplayStreamInfo;

/**
 * One recorded replay, as surfaced to UI / tooling. Built from the network replay streamer's
 * FNetworkReplayStreamInfo plus this module's sidecar timeline metadata. Flat and copyable — no
 * UObject refs — so it is safe to hand to view-models and Blueprint.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSREPLAY_API FRep_ReplayInfo
{
	GENERATED_BODY()

	/** Stream name — the stable id passed to PlayReplay / DeleteReplay. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Replay")
	FString Name;

	/** Friendly display name recorded into the demo header. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Replay")
	FText FriendlyName;

	/** Map the replay was recorded on (long package name leaf), if known. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Replay")
	FString MapName;

	/** Total recorded length in seconds (from the streamer's LengthInMS). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Replay")
	float DurationSeconds = 0.f;

	/** Wall-clock time the recording was made. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Replay")
	FDateTime Timestamp;

	/** On-disk size in bytes (informational). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Replay")
	int64 SizeInBytes = 0;

	/** True if the streamer reported this stream as still being recorded (live). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Replay")
	bool bIsLive = false;

	/** Number of timeline marker events this module recorded alongside the demo (0 if no sidecar). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Replay")
	int32 TimelineEventCount = 0;
};

/** Fired after the recorded-replay registry is (re)built by RefreshReplays. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRep_OnReplayRegistryUpdated);
/** Fired when recording starts/stops or playback starts (param: the affected replay name). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRep_OnReplayStateChanged, const FString&, ReplayName);

/**
 * URep_ReplaySubsystem — a clean tag/metadata facade over the ENGINE demo driver.
 *
 * WRAPS, does not reinvent: every record/play call forwards to UGameInstance's built-in
 * StartRecordingReplay / StopRecordingReplay / PlayReplay, and the recorded-replay registry is
 * built from the active INetworkReplayStreamer's EnumerateStreams. This module never touches the
 * net format, the demo net driver, or serialization of actor state — that is all the engine's.
 *
 * On top of the engine it provides:
 *  - StartRecording(Name)/StopRecording with friendly-name + map metadata defaulting from settings.
 *  - PlayReplay(Name) with a guard that recording and playback are mutually exclusive.
 *  - A queryable registry (GetReplays / RefreshReplays) for UI.
 *  - An owned URep_ReplayTimeline that harvests significant gameplay events while recording and
 *    exposes them for the scrubber during playback.
 *  - A weakly-held list of IRep_ReplayEventSource contributors (GI-scoped cross-world refs are held
 *    WEAKLY and pruned — never as hard TScriptInterface).
 *
 * MULTIPLAYER REPLAY CAVEATS (read before shipping):
 *  - Replays are RECORDED ON THE SERVER (or listen-server host). A pure client cannot record the
 *    authoritative demo; StartRecording on a client is rejected. Drive recording from server-side
 *    code (e.g. game mode) and distribute the resulting demo out-of-band, or use a replay streamer
 *    backend that uploads from the server.
 *  - PLAYBACK is always LOCAL: PlayReplay spins up a client-only UDemoNetDriver in this game
 *    instance; it does not affect other connected machines. Do not call PlayReplay on a live
 *    multiplayer session's primary world — travel to a dedicated playback context first.
 *  - The demo only contains what the server marked NetRelevant + replicated. Anything client-only
 *    (UI, cosmetic-only state) is NOT in the demo; that is exactly why this module layers a sidecar
 *    timeline rather than trying to reconstruct gameplay events from the net stream.
 *  - This subsystem is a GameInstance subsystem and holds NO replicated state (replicated state
 *    lives on components/AInfo only, per the plugin's rules); it is a local controller of the
 *    engine demo driver.
 */
UCLASS()
class DESIGNPATTERNSREPLAY_API URep_ReplaySubsystem : public UDP_GameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	// ---- Recording ----

	/**
	 * Start recording the current session to a demo named Name (empty => an auto name from settings
	 * with a timestamp suffix). Forwards to UGameInstance::StartRecordingReplay. Rejected (returns
	 * false, logged) if already recording, if a replay is currently playing, or if this machine is a
	 * pure client (replays must be recorded with authority). The owned timeline begins harvesting.
	 *
	 * @param Name           Desired stream name; empty uses the settings default + timestamp.
	 * @param FriendlyName   Optional display name; empty uses the settings template with {Map}.
	 * @return The actual stream name recording was started under, or empty on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay")
	FString StartRecording(const FString& Name, const FText& FriendlyName);

	/** Stop the in-progress recording (forwards to StopRecordingReplay) and flush the timeline sidecar. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay")
	void StopRecording();

	/** True while a demo is being recorded in this game instance. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Replay")
	bool IsRecording() const;

	// ---- Playback ----

	/**
	 * Begin local playback of the demo named Name (forwards to UGameInstance::PlayReplay). Rejected
	 * if currently recording. On success the world travels into a client-only demo playback session;
	 * use URep_PlaybackController (GetPlaybackController) to drive speed/seek/pause afterwards.
	 *
	 * @return True if PlayReplay was issued (the actual load is async; listen for PlaybackStarted).
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay")
	bool PlayReplay(const FString& Name);

	/** True while this game instance is playing back a demo (a UDemoNetDriver is present). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Replay")
	bool IsPlaying() const;

	// ---- Registry ----

	/**
	 * Kick an async enumeration of stored replays via the active streamer's EnumerateStreams. The
	 * registry is rebuilt when the streamer responds and OnReplayRegistryUpdated fires. Safe to call
	 * repeatedly; overlapping requests coalesce.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay")
	void RefreshReplays();

	/** The last-enumerated set of recorded replays. Call RefreshReplays to repopulate. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Replay")
	const TArray<FRep_ReplayInfo>& GetReplays() const { return Replays; }

	/** Look up a single replay's info by name. Returns false if not in the current registry. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay")
	bool FindReplay(const FString& Name, FRep_ReplayInfo& OutInfo) const;

	/** Delete a stored replay (forwards to the streamer's DeleteFinishedStream) and refresh. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay")
	void DeleteReplay(const FString& Name);

	// ---- Event sources ----

	/**
	 * Register a contributor of curated timeline events. Held WEAKLY (TWeakInterfacePtr) and pruned
	 * when the implementer dies — never a hard reference, since sources may be world-scoped objects
	 * and this subsystem outlives worlds. Polled by the timeline when recording begins.
	 */
	void RegisterEventSource(const TScriptInterface<IRep_ReplayEventSource>& Source);

	/** Remove a previously-registered event source (also happens automatically on GC prune). */
	void UnregisterEventSource(const TScriptInterface<IRep_ReplayEventSource>& Source);

	/** Append every live event source's known events into OutEvents (used by the timeline). */
	void GatherFromEventSources(float RecordingTimeSeconds, TArray<struct FRep_ReplayEvent>& OutEvents);

	// ---- Owned helpers ----

	/** The owned timeline recorder/reader. Always valid after Initialize. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Replay")
	URep_ReplayTimeline* GetTimeline() const { return Timeline; }

	// ---- Delegates ----

	/** Broadcast after RefreshReplays rebuilds the registry. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Replay")
	FRep_OnReplayRegistryUpdated OnReplayRegistryUpdated;

	/** Broadcast when recording starts/stops or playback starts. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Replay")
	FRep_OnReplayStateChanged OnReplayStateChanged;

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

private:
	/** The owned timeline; harvests bus/command events while recording. NewObject'd at Initialize. */
	UPROPERTY()
	TObjectPtr<URep_ReplayTimeline> Timeline = nullptr;

	/** Last-enumerated registry of stored replays. */
	UPROPERTY()
	TArray<FRep_ReplayInfo> Replays;

	/**
	 * Weakly-held curated event sources. GI-scoped cross-world refs are held weakly and pruned so a
	 * dead world's objects can never leak through this subsystem.
	 */
	TArray<TWeakInterfacePtr<IRep_ReplayEventSource>> EventSources;

	/** The name of the demo currently being recorded (empty when not recording). */
	FString ActiveRecordingName;

	/** True while an EnumerateStreams request is in flight, to coalesce overlapping refreshes. */
	bool bEnumerationInFlight = false;

	/** Resolve the active network replay streamer for the current game instance, or null. */
	TSharedPtr<INetworkReplayStreamer> GetActiveStreamer() const;

	/** Build the auto replay name (settings default + timestamp) when a caller passes none. */
	FString BuildAutoReplayName() const;

	/** Build the friendly name from the settings template, substituting the current map. */
	FText BuildFriendlyName(const FText& Provided) const;

	/** True if this game instance has authority to record (server / standalone, not a pure client). */
	bool CanRecordHere() const;

	/** Drop dead weak event-source entries. */
	void PruneEventSources();

	/** Async callback that turns enumerated streams into the registry and broadcasts the update. */
	void OnStreamsEnumerated(const TArray<FNetworkReplayStreamInfo>& Streams);
};
