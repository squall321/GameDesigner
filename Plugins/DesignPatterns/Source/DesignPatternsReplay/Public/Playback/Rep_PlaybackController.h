// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "Rep_PlaybackController.generated.h"

class URep_ReplayTimeline;
class UDemoNetDriver;
class UWorld;
struct FRep_ReplayEvent;

/** Fired when playback transport state changes (speed, pause, or a seek completes). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRep_OnPlaybackTransportChanged);

/**
 * URep_PlaybackController — transport control over the engine's replay playback.
 *
 * Thin wrapper around the active UDemoNetDriver / AWorldSettings demo state. It does NOT decode the
 * demo itself — it asks the engine to set playback speed, pause/resume, and jump (GotoTimeInSeconds)
 * exactly as the engine's own replay tools do. All state queried (current time, total time, paused)
 * comes from the demo net driver so it always reflects the real engine playback position.
 *
 * Created and owned by whatever drives the replay UI (typically alongside URep_ReplayViewModel);
 * it holds the playing UWorld weakly and re-resolves the demo driver each call, so it is robust to
 * the world being torn down between scrubber interactions.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSREPLAY_API URep_PlaybackController : public UObject
{
	GENERATED_BODY()

public:
	/** Bind this controller to the world currently playing a demo. Safe to re-bind on world change. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Playback")
	void BindToWorld(UWorld* InWorld);

	/** True if a demo is currently playing in the bound world (a UDemoNetDriver is present). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Replay|Playback")
	bool IsPlaybackActive() const;

	// ---- Transport ----

	/**
	 * Set playback speed (1.0 = real time). Clamped to the settings [Min,Max] range. 0 is treated as
	 * a pause request (forwarded to SetPaused(true)) since the demo driver does not run at 0x.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Playback")
	void SetPlaybackSpeed(float Speed);

	/** Current playback speed (the world settings DemoPlayTimeDilation), or 0 while paused. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Replay|Playback")
	float GetPlaybackSpeed() const;

	/** Pause or resume playback (forwards to the demo driver's pause API). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Playback")
	void SetPaused(bool bPaused);

	/** True while playback is paused. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Replay|Playback")
	bool IsPaused() const;

	/** Toggle pause/resume. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Playback")
	void TogglePause();

	// ---- Seeking ----

	/**
	 * Jump playback to an absolute time in seconds (forwards to UDemoNetDriver::GotoTimeInSeconds).
	 * Clamped to [0, total length]. The engine streams the needed checkpoint/data; the seek is async
	 * but the controller reports the requested target immediately for responsive UI.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Playback")
	void SeekToTime(float TimeSeconds);

	/**
	 * Seek to a timeline event. Lands SeekToEventLeadInSeconds (settings) BEFORE the event so the
	 * lead-up is visible. Pass the event resolved from the timeline (FindNext/PreviousEvent).
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Playback")
	void SeekToEvent(const FRep_ReplayEvent& Event);

	/** Restart playback from the beginning (SeekToTime 0). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Playback")
	void Restart();

	// ---- Queries ----

	/** Current playback position in seconds (from the demo driver). 0 if not playing. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Replay|Playback")
	float GetCurrentTime() const;

	/** Total demo length in seconds (from the demo driver). 0 if not playing/known. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Replay|Playback")
	float GetTotalTime() const;

	/** Normalized playback position in [0,1]; 0 when total length is unknown. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Replay|Playback")
	float GetNormalizedPosition() const;

	/** Fires after any transport change (speed/pause/seek request). */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Replay|Playback")
	FRep_OnPlaybackTransportChanged OnTransportChanged;

private:
	/** The world playing the demo. Weak so a torn-down playback world is detected, not dangling. */
	UPROPERTY()
	TWeakObjectPtr<UWorld> PlaybackWorld;

	/** Last speed we requested (cached so a paused-then-resumed controller restores it). */
	float CachedSpeed = 1.f;

	/** True while we have explicitly paused (engine pause is a 0 dilation we mirror here). */
	bool bPaused = false;

	/** Resolve the active UDemoNetDriver from the bound world, or null if not in playback. */
	UDemoNetDriver* GetDemoDriver() const;

	/** Clamp a requested speed to the settings range (defensive defaults if the CDO is null). */
	float ClampSpeed(float Speed) const;
};
