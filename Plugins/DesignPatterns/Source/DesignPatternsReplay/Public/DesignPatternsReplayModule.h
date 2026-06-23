// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "NativeGameplayTags.h"

/**
 * Replay module for the DesignPatterns plugin.
 *
 * A thin, opinionated FACADE over the engine's built-in demo-recording pipeline
 * (UGameInstance::StartRecordingReplay / StopRecordingReplay / PlayReplay and the
 * pluggable network replay streamer). The module deliberately does NOT reinvent any
 * networking: recording, the on-wire demo format and playback net-driver are 100% the
 * engine's. On top of that it adds:
 *
 *  - URep_ReplaySubsystem    : a clean tag/metadata API to start/stop recording, list the
 *                              recorded demos (via the streamer's EnumerateStreams) and play
 *                              one back by name.
 *  - URep_ReplayTimeline     : records significant gameplay events (message-bus broadcasts +
 *                              the core command history) into a sidecar event timeline so a
 *                              scrubber can show markers and jump.
 *  - URep_PlaybackController  : playback speed / pause / SeekToTime / SeekToEvent over the
 *                              engine's UDemoNetDriver and world-settings replay state.
 *  - URep_ReplayViewModel     : an MVVM scrubber view-model (position / duration / markers).
 *  - URep_SpectatorController : a lightweight spectator / free-cam during playback that
 *                              integrates with the Camera module ONLY through the
 *                              IRep_SpectatorCamera seam (resolved from the service locator).
 *
 * Cross-module coupling is exclusively through seams (this module's own IRep_SpectatorCamera /
 * IRep_ReplayEventSource and the shared Seams module) and the message bus — never a hard
 * include of a sibling high-level module's concrete header.
 */
class FDesignPatternsReplayModule : public IModuleInterface
{
public:
	//~ Begin IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface
};

/**
 * Native (C++-defined) anchor tags for the DesignPatternsReplay module.
 *
 * All tags are rooted under the conventional DP.* trees so tag-hierarchy matching works at
 * startup and the keys are stable/designer-visible:
 *  - DP.Service.Replay.*  : service-locator keys (the subsystem, the spectator-camera seam).
 *  - DP.Bus.Replay.*      : message-bus channels the replay system broadcasts on.
 *  - Rep.Event.*          : built-in timeline event identity tags contributed by this module.
 *
 * Concrete event-source tags games add live in their own tag config; this header only anchors
 * the roots the plumbing relies on. Full strings are defined in DesignPatternsReplayModule.cpp.
 */
namespace Rep_NativeTags
{
	// --- Service-locator keys (children of the core DP.Service root) ---

	/** Service key under which URep_ReplaySubsystem registers its IRep_ReplayEventSource aggregate. */
	DESIGNPATTERNSREPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_Replay);

	/**
	 * Service key the game (or the Camera module) registers an IRep_SpectatorCamera implementation
	 * under, so the spectator controller can drive a free-cam without hard-including Camera.
	 */
	DESIGNPATTERNSREPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_SpectatorCamera);

	/**
	 * Service key under which URep_HighlightSubsystem registers itself as an IRep_ReplayEventSource so
	 * the timeline can poll auto-detected highlight markers, and a project can resolve the highlight
	 * system to drive clips/reels.
	 */
	DESIGNPATTERNSREPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_Replay_Highlights);

	/**
	 * Service key a thumbnail-capture adapter registers an ISeam_ReplayThumbnailSource under, so the
	 * share service can grab a share-card thumbnail without depending on RenderCore/RHI.
	 */
	DESIGNPATTERNSREPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_Replay_Thumbnail);

	// --- Message-bus channels (children of the core DP.Bus root) ---

	/** Root of the bus subtree the timeline subscribes to for "significant" gameplay events. */
	DESIGNPATTERNSREPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Replay);

	/** Broadcast when recording starts (payload: replay name). */
	DESIGNPATTERNSREPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Replay_RecordingStarted);

	/** Broadcast when recording stops (payload: replay name). */
	DESIGNPATTERNSREPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Replay_RecordingStopped);

	/** Broadcast when playback starts (payload: replay name). */
	DESIGNPATTERNSREPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Replay_PlaybackStarted);

	/**
	 * Broadcast (locally) when the killcam component observes its owner's death, so the local killcam
	 * director can begin a death-cam. Carries no replicated payload — it is produced from already-
	 * replicated death state and re-broadcast locally.
	 */
	DESIGNPATTERNSREPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Replay_Death);

	/** Broadcast when the highlight detector promotes a window into a saved highlight moment. */
	DESIGNPATTERNSREPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Replay_HighlightDetected);

	// --- Built-in timeline event identity tags (Rep.Event.* root) ---

	/** Identity root for replay timeline marker events. */
	DESIGNPATTERNSREPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event);

	/** A bookmark/marker the recording side dropped (manual or scripted). */
	DESIGNPATTERNSREPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Bookmark);

	/** A gameplay command was recorded (sourced from the core command history). */
	DESIGNPATTERNSREPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Command);

	/** A message-bus event was promoted into the timeline. */
	DESIGNPATTERNSREPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_BusMessage);

	/** A chapter marker the director/bookmarks dropped (a coarse, designer/viewer-authored section). */
	DESIGNPATTERNSREPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Chapter);

	/** A death event recorded onto the timeline (drives killcam framing and highlight scoring). */
	DESIGNPATTERNSREPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Event_Death);

	// --- Auto-detected highlight identity tags (Rep.Highlight.* root) ---
	//
	// These are recorded back onto the timeline with a DISTINCT root from the source Rep.Event.* tags
	// the detector ingests, so a promoted highlight marker can never be re-ingested and re-promoted.

	/** Identity root for promoted highlight markers (distinct from the ingested Rep.Event.* root). */
	DESIGNPATTERNSREPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Highlight);

	/** A multi-kill highlight (several scoring events inside the rule-set window). */
	DESIGNPATTERNSREPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Highlight_MultiKill);

	/** A clutch highlight (a decisive event late in / against the odds of an encounter). */
	DESIGNPATTERNSREPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Highlight_Clutch);

	/** An objective highlight (a captured/completed objective worth surfacing). */
	DESIGNPATTERNSREPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Highlight_Objective);

	// --- Analytics event tags emitted by the highlight system (Rep.Analytics.* root) ---

	/** Aggregate analytics event recorded when a highlight is detected (count + score attrs). */
	DESIGNPATTERNSREPLAY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Analytics_HighlightDetected);
}
