// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Highlight/Rep_HighlightTypes.h"
#include "Rep_ClipController.generated.h"

class URep_PlaybackController;

/** Fired when a clip starts playing (param: the moment being played). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRep_OnClipStarted, const FRep_HighlightMoment&, Moment);
/** Fired when a playing clip reaches its out-point (and playback is paused at the out-point). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRep_OnClipFinished);

/**
 * URep_ClipController — plays a single highlight clip (an in/out window) over the engine demo playback.
 *
 * It does NOT decode the demo itself: it drives the existing URep_PlaybackController (SeekToTime +
 * speed) to seek to a moment's in-point, play forward, and pause at the out-point. The owner ticks
 * Tick(DeltaSeconds) each frame so the controller can detect the out-point from the live playback time
 * (the demo seek/play is async, so a wall-clock timer would drift; we poll the real position instead).
 *
 * Owned by URep_HighlightSubsystem (which fan-ticks it via FTickableGameObject); never standalone.
 * Everything is LOCAL and cosmetic — playback is client-side.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSREPLAY_API URep_ClipController : public UObject
{
	GENERATED_BODY()

public:
	/** Bind the transport controller this clip controller drives. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Highlight")
	void BindPlayback(URep_PlaybackController* InController);

	/**
	 * Begin playing Moment's clip window: seeks to its in-point, sets playback speed, and starts
	 * watching for the out-point. No-op if no playback controller is bound or playback is inactive.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Highlight")
	void PlayClip(const FRep_HighlightMoment& Moment);

	/** Stop watching the current clip (does not change playback state). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Highlight")
	void StopClip();

	/** True while a clip is being played/watched. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Replay|Highlight")
	bool IsPlayingClip() const { return bPlaying; }

	/** Called each frame by the owning subsystem; detects the out-point and pauses there. */
	void Tick(float DeltaSeconds);

	/** Fires when a clip starts. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Replay|Highlight")
	FRep_OnClipStarted OnClipStarted;

	/** Fires when a clip reaches its out-point. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Replay|Highlight")
	FRep_OnClipFinished OnClipFinished;

private:
	/** The transport controller (weak: the owner owns lifetime). */
	UPROPERTY(Transient)
	TWeakObjectPtr<URep_PlaybackController> Playback;

	/** The moment currently being played. */
	UPROPERTY(Transient)
	FRep_HighlightMoment CurrentMoment;

	/** True while watching for the out-point. */
	bool bPlaying = false;

	/** Real-time guard: max seconds we keep watching before giving up (avoids a stuck async seek). */
	float ElapsedWatchSeconds = 0.f;
};
