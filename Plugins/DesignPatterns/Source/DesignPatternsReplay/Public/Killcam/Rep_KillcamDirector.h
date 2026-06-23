// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Killcam/Rep_KillcamComponent.h"
#include "UObject/WeakInterfacePtr.h"
#include "Rep_KillcamDirector.generated.h"

class URep_PlaybackController;
class URep_SpectatorController;
class APlayerController;
class AActor;
class ISeam_FloatingTextFeed;

/** Fired when a death-cam begins. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRep_OnKillcamStarted, const FRep_KillcamRecord&, Record);
/** Fired when a death-cam ends and control returns to live. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRep_OnKillcamEnded);

/**
 * URep_KillcamDirector — LOCAL orchestrator of the death-cam.
 *
 * On a death (observed via a URep_KillcamComponent's OnKillcamDeath, or driven manually) it:
 *  1. rewinds playback to (death time - KillcamLookbackSeconds) via URep_PlaybackController::SeekToTime,
 *  2. enters the spectator/free-cam through URep_SpectatorController and frames the killer via the
 *     IRep_SpectatorCamera seam (FocusOnActor) — NEVER hard-including the Camera module,
 *  3. plays the lookback window at the configured speed, then
 *  4. on reaching the death moment, exits the spectator and returns control to live.
 *
 * It does NOT decode the demo: rewind/play is the engine's via the playback controller, and framing is
 * the spectator controller's. The owner ticks Tick(DeltaSeconds) so the director can detect the
 * return-point from the real playback time (the seek is async).
 *
 * Optionally re-surfaces the lethal hit as a floating "damage number" through the ISeam_FloatingTextFeed
 * seam during the death-cam (resolved weakly from the locator; absent => no-op).
 *
 * Everything is LOCAL and cosmetic — replay/spectator is client-side and never replicated.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSREPLAY_API URep_KillcamDirector : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Wire the director to the transport + spectator controllers and the local player it frames for.
	 * Re-bindable. The playback/spectator controllers are owned by the caller (the replay UI host).
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Killcam")
	void Initialize(URep_PlaybackController* InPlayback, URep_SpectatorController* InSpectator, APlayerController* InLocalController);

	/**
	 * Auto-bind to a kill-cam component's death delegate so a death triggers BeginKillcam automatically.
	 * Safe to call once; rebinding replaces the prior binding.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Killcam")
	void WatchComponent(URep_KillcamComponent* Component);

	/**
	 * Begin a death-cam for Record: rewind, enter spectator framing on the killer, play the lookback,
	 * return to live at the death moment. Resolves the killer actor (if any) for framing. No-op if the
	 * killcam is disabled in settings, no playback controller is bound, or the record is invalid.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Killcam")
	void BeginKillcam(const FRep_KillcamRecord& Record);

	/** Abort an in-progress death-cam and return to live immediately. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Killcam")
	void EndKillcam();

	/** True while a death-cam is running. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Replay|Killcam")
	bool IsKillcamActive() const { return bActive; }

	/** Resolve the killer actor for the current record (best-effort) so a caller can frame externally. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Killcam")
	void SetKillerActorForFraming(AActor* KillerActor);

	/** Called each frame by the owner; detects the return-point and ends the death-cam there. */
	void Tick(float DeltaSeconds);

	/** Fired when a death-cam begins. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Replay|Killcam")
	FRep_OnKillcamStarted OnKillcamStarted;

	/** Fired when a death-cam ends. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Replay|Killcam")
	FRep_OnKillcamEnded OnKillcamEnded;

private:
	/** Transport controller (weak: owned by the host). */
	UPROPERTY(Transient)
	TWeakObjectPtr<URep_PlaybackController> Playback;

	/** Spectator controller (weak: owned by the host) used to frame the killer via the camera seam. */
	UPROPERTY(Transient)
	TWeakObjectPtr<URep_SpectatorController> Spectator;

	/** Local player controller the death-cam runs for. */
	UPROPERTY(Transient)
	TWeakObjectPtr<APlayerController> LocalController;

	/** The watched kill-cam component (weak). */
	UPROPERTY(Transient)
	TWeakObjectPtr<URep_KillcamComponent> Watched;

	/** The killer actor to frame (best-effort; may be null => keep victim view). */
	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> KillerActor;

	/** The record currently being replayed. */
	UPROPERTY(Transient)
	FRep_KillcamRecord CurrentRecord;

	/** True while a death-cam is running. */
	bool bActive = false;

	/** The demo time we should return to live at (the death moment). */
	float ReturnAtTimeSeconds = 0.f;

	/** Real-time watchdog so a stuck async seek cannot pin the death-cam forever. */
	float ElapsedSeconds = 0.f;

	/** UFUNCTION bound to the watched component's OnKillcamDeath. */
	UFUNCTION()
	void HandleDeath(const FRep_KillcamRecord& Record);

	/** Push the lethal-hit damage number through the floating-text feed seam (if registered). */
	void EmitLethalFloatingText(const FRep_KillcamRecord& Record);

	/** Resolve the floating-text feed seam from the locator (weak), or null. */
	ISeam_FloatingTextFeed* ResolveFloatingTextFeed() const;
};
