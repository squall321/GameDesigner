// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "Rep_DeveloperSettings.generated.h"

/**
 * Project-wide configuration for the DesignPatternsReplay module. Appears under
 * Project Settings -> Plugins -> Design Patterns Replay. Edited with no code.
 *
 * Every gameplay/UX number the replay system uses is a tunable here (or on a per-instance
 * UPROPERTY) — there are no magic literals in the implementation. Where a setting is consulted
 * with a CDO that may be null in an editor/early-load context, the call site documents the
 * defensive fallback it uses instead.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Design Patterns Replay"))
class DESIGNPATTERNSREPLAY_API URep_DeveloperSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	URep_DeveloperSettings();

	virtual FName GetCategoryName() const override { return FName("Plugins"); }

	/** Convenience CDO accessor. Never null in a configured project; call sites still null-check. */
	static const URep_DeveloperSettings* Get();

	// ---- Recording ----

	/**
	 * Base name used when a caller starts recording without supplying one (StartRecording with an
	 * empty name). A timestamp suffix is appended so successive auto-recordings do not collide.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Recording")
	FString DefaultReplayName = TEXT("DPReplay");

	/**
	 * Friendly display name template for auto-named recordings. {Map} is substituted with the
	 * current map name. Stored in the demo header and shown in the replay registry.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Recording")
	FString DefaultFriendlyNameTemplate = TEXT("Replay - {Map}");

	/**
	 * When true the timeline subscribes to the message bus (under the configured root) and the
	 * core command history to auto-populate timeline markers while recording.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Timeline")
	bool bAutoRecordTimeline = true;

	/**
	 * The bus subtree the timeline listens to for "significant" events while recording. Defaults to
	 * the module's DP.Bus.Replay root so games opt events in by broadcasting there; widen to DP.Bus
	 * to capture everything (heavier). Consulted defensively: empty => the module root is used.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Timeline", meta = (Categories = "DP.Bus"))
	FGameplayTag TimelineBusRoot;

	/**
	 * Hard cap on timeline events kept per recording so a long session cannot grow the sidecar
	 * unbounded. Oldest non-bookmark events are dropped first when exceeded. 0 = unbounded.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Timeline", meta = (ClampMin = "0"))
	int32 MaxTimelineEvents = 4096;

	// ---- Playback ----

	/** Default playback speed applied when playback starts (1.0 = real time). */
	UPROPERTY(EditAnywhere, Config, Category = "Playback", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "8.0"))
	float DefaultPlaybackSpeed = 1.f;

	/** Lowest playback speed the controller will clamp to (above 0; 0 would equal pause). */
	UPROPERTY(EditAnywhere, Config, Category = "Playback", meta = (ClampMin = "0.01", UIMin = "0.01"))
	float MinPlaybackSpeed = 0.1f;

	/** Highest playback speed the controller will clamp to. */
	UPROPERTY(EditAnywhere, Config, Category = "Playback", meta = (ClampMin = "0.1", UIMin = "0.1"))
	float MaxPlaybackSpeed = 8.f;

	/**
	 * When seeking to an event the scrubber lands this many seconds BEFORE the event time, so the
	 * lead-up is visible rather than starting mid-action. Clamped to [0, event time].
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Playback", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SeekToEventLeadInSeconds = 1.5f;

	// ---- Spectator ----

	/**
	 * Free-cam movement speed (cm/s) used by the inert engine-pawn fallback when no
	 * IRep_SpectatorCamera is registered. Ignored when a camera adapter is present.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Spectator", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float FallbackFreeCamSpeed = 1200.f;

	/** Spectator pawn class the fallback possesses for free-fly. Null => engine ASpectatorPawn. */
	UPROPERTY(EditAnywhere, Config, Category = "Spectator", meta = (AllowAbstract = "false"))
	TSoftClassPtr<APawn> FallbackSpectatorPawnClass;

	// ---- Killcam ----

	/** Master enable for the death-cam: when false the killcam component never auto-replays on death. */
	UPROPERTY(EditAnywhere, Config, Category = "Killcam")
	bool bEnableKillcam = true;

	/**
	 * How many seconds before the death moment the killcam rewinds to and plays back from. Clamped to
	 * the available recorded length at runtime. The killcam returns to live after replaying this window.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Killcam", meta = (ClampMin = "0.5", UIMin = "0.5", UIMax = "30.0"))
	float KillcamLookbackSeconds = 6.f;

	/** Playback speed used while the death-cam plays its lookback window (1.0 = real time). */
	UPROPERTY(EditAnywhere, Config, Category = "Killcam", meta = (ClampMin = "0.1", UIMin = "0.1", UIMax = "4.0"))
	float KillcamPlaybackSpeed = 1.f;

	/**
	 * When true the killcam frames from the KILLER's point of view (via the spectator camera seam),
	 * else it keeps the victim's last view. Cosmetic; purely local.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Killcam")
	bool bKillcamFollowKiller = true;

	// ---- Highlights ----

	/** Master enable for auto highlight detection from tagged timeline events. */
	UPROPERTY(EditAnywhere, Config, Category = "Highlights")
	bool bEnableHighlightDetection = true;

	/** The highlight rule-set data asset (windows/scores) the detector reads. Null => no auto-detection. */
	UPROPERTY(EditAnywhere, Config, Category = "Highlights", meta = (AllowedClasses = "/Script/DesignPatternsReplay.Rep_HighlightRuleSet"))
	TSoftObjectPtr<class URep_HighlightRuleSet> HighlightRuleSet;

	/**
	 * Hard cap on highlight moments retained per session so a long match cannot grow the reel
	 * unbounded. Lowest-scoring moments are dropped first when exceeded. 0 = unbounded.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Highlights", meta = (ClampMin = "0"))
	int32 MaxRetainedHighlights = 64;

	/**
	 * Seconds of lead-in captured before a detected highlight's anchor time when exporting a clip, so
	 * the clip starts with context. Clamped to [0, anchor time].
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Highlights", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float HighlightClipLeadInSeconds = 3.f;

	/**
	 * Seconds of lead-out captured after a detected highlight's anchor time when exporting a clip.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Highlights", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float HighlightClipLeadOutSeconds = 2.f;

	// ---- Director (multi-camera) ----

	/**
	 * Seconds the auto-director dwells on one camera target before cycling to the next when running in
	 * auto-cycle mode. Manual director cycling ignores this.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Director", meta = (ClampMin = "0.5", UIMin = "0.5"))
	float DirectorAutoCycleSeconds = 8.f;

	// ---- Share / Export ----

	/** Pixel dimensions requested for a share-card thumbnail (via the thumbnail seam / screenshot fallback). */
	UPROPERTY(EditAnywhere, Config, Category = "Share", meta = (ClampMin = "16"))
	FIntPoint ShareThumbnailSize = FIntPoint(640, 360);

	/**
	 * How long (seconds) the share service waits for an async thumbnail before writing the descriptor
	 * without one. Prevents a never-ready capture from leaving an export pending forever.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Share", meta = (ClampMin = "0.5", UIMin = "0.5"))
	float ShareThumbnailTimeoutSeconds = 5.f;

	// ---- Analytics ----

	/** When true the highlight system forwards a PII-safe aggregate event to the analytics sink on detect. */
	UPROPERTY(EditAnywhere, Config, Category = "Analytics")
	bool bForwardHighlightsToAnalytics = true;

	/**
	 * Service-locator tag under which the host registers its ISeam_AnalyticsSink adapter. The highlight
	 * system resolves the sink WEAKLY under this key. Unset/unresolved => analytics forwarding is a
	 * documented no-op (highlights still work locally). Mirrors the other modules' sink-key convention.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Analytics", meta = (Categories = "DP.Service"))
	FGameplayTag AnalyticsSinkServiceTag;

	// ---- Debug ----

	/** When true the replay subsystem and friends start with verbose logging enabled. */
	UPROPERTY(EditAnywhere, Config, Category = "Debug")
	bool bVerboseLogging = false;
};
