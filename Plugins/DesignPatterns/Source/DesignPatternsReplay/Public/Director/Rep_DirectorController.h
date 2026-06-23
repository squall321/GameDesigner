// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "Rep_DirectorController.generated.h"

class URep_SpectatorController;
class APlayerController;
class APawn;
class AActor;

/** The camera mode the multi-camera director is presenting. */
UENUM(BlueprintType)
enum class ERep_DirectorMode : uint8
{
	/** Follow a specific player/pawn target (cycled among the recorded participants). */
	FollowPlayer,

	/** Free-fly camera the viewer controls manually (the spectator free-cam). */
	FreeCam,

	/** A pulled-back tactical/overhead framing on a chosen anchor. */
	Tactical,

	/** A scripted cinematic framing (slow, composed) on a chosen anchor. */
	Cinematic
};

/** Fired when the director's mode or follow target changes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRep_OnDirectorModeChanged);

/**
 * URep_DirectorController — a multi-camera "director" for replay playback: cycle through the recorded
 * players, drop into free-cam, or present a tactical / cinematic framing.
 *
 * It frames EXCLUSIVELY through the URep_SpectatorController (which itself routes to the project camera
 * via the IRep_SpectatorCamera seam). It NEVER hard-includes the DesignPatternsCamera module or its
 * ICam_CameraModeProvider — richer modes (tactical/cinematic) are expressed as director intents that the
 * spectator camera seam adapter interprets (or that degrade to a plain view-target focus in the inert
 * engine fallback).
 *
 * The follow-target list is the set of player pawns the caller supplies (RefreshTargets) — typically the
 * replay's recorded player controllers' pawns. Auto-cycle dwells DirectorAutoCycleSeconds per target.
 *
 * Owned by the replay UI host; holds the spectator controller weakly. All LOCAL and cosmetic.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSREPLAY_API URep_DirectorController : public UObject
{
	GENERATED_BODY()

public:
	/** Bind the spectator controller (framing) and the local player controller the director runs for. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Director")
	void Initialize(URep_SpectatorController* InSpectator, APlayerController* InLocalController);

	/**
	 * Set the list of follow targets (the recorded participants' pawns/actors) the director cycles among.
	 * Null entries are ignored. Resets the cycle index to the first valid target.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Director")
	void RefreshTargets(const TArray<AActor*>& InTargets);

	// ---- Mode control ----

	/** Switch the director mode, re-applying framing on the current target. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Director")
	void SetMode(ERep_DirectorMode NewMode);

	/** The current director mode. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Replay|Director")
	ERep_DirectorMode GetMode() const { return Mode; }

	/** Cycle to the next follow target (wraps). Switches to FollowPlayer mode if not already framing one. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Director")
	void CycleNextTarget();

	/** Cycle to the previous follow target (wraps). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Director")
	void CyclePreviousTarget();

	/** Frame a specific target actor (sets FollowPlayer mode). No-op if Target is null. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Director")
	void FocusTarget(AActor* Target);

	// ---- Auto-cycle ----

	/** Enable/disable auto-cycling among follow targets (dwell from settings). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Director")
	void SetAutoCycle(bool bEnabled);

	/** True while auto-cycling. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Replay|Director")
	bool IsAutoCycling() const { return bAutoCycle; }

	/** Called each frame by the owner; advances the auto-cycle timer. */
	void Tick(float DeltaSeconds);

	/** The currently-framed target actor, or null. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Replay|Director")
	AActor* GetCurrentTarget() const;

	/** Fired when the mode or follow target changes. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Replay|Director")
	FRep_OnDirectorModeChanged OnModeChanged;

private:
	/** The spectator controller used to frame (weak: owned by the host). */
	UPROPERTY(Transient)
	TWeakObjectPtr<URep_SpectatorController> Spectator;

	/** The local player controller the director runs for (weak). */
	UPROPERTY(Transient)
	TWeakObjectPtr<APlayerController> LocalController;

	/** The follow targets (weak — the recorded participants live in the playback world). */
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<AActor>> Targets;

	/** Current cycle index into Targets. */
	int32 CurrentIndex = 0;

	/** Current mode. */
	ERep_DirectorMode Mode = ERep_DirectorMode::FollowPlayer;

	/** True while auto-cycling among targets. */
	bool bAutoCycle = false;

	/** Seconds spent on the current target while auto-cycling. */
	float DwellSeconds = 0.f;

	/** Ensure the spectator is active (entered) for framing; returns false if it could not be entered. */
	bool EnsureSpectating();

	/** Apply framing for the current mode + target through the spectator controller. */
	void ApplyFraming();

	/** Drop dead weak targets and clamp the cycle index. */
	void PruneTargets();
};
