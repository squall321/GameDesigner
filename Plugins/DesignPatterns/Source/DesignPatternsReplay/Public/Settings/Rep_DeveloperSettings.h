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

	// ---- Debug ----

	/** When true the replay subsystem and friends start with verbose logging enabled. */
	UPROPERTY(EditAnywhere, Config, Category = "Debug")
	bool bVerboseLogging = false;
};
