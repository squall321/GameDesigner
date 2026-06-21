// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "UObject/WeakInterfacePtr.h"
#include "Rep_SpectatorController.generated.h"

class APlayerController;
class APawn;
class IRep_SpectatorCamera;

/**
 * URep_SpectatorController — a lightweight spectator / free-cam mode for replay playback.
 *
 * During demo playback the local player normally watches from a recorded view target. This object
 * lets the viewer break free into a spectator camera. It integrates with the project's camera
 * system ONLY through the IRep_SpectatorCamera seam, resolved from the service locator under
 * Rep_NativeTags::Service_SpectatorCamera — so it never hard-includes the DesignPatternsCamera
 * module. The Camera module (or the game) registers an adapter that pushes a free-fly camera mode.
 *
 * INERT DEFAULT (documented): when NO IRep_SpectatorCamera is registered, the controller degrades
 * to an engine-only fallback — it spawns/possesses an ASpectatorPawn (class from settings, else the
 * engine default) on the local player controller so free-fly still works without the Camera module.
 * On exit it restores the prior view target / pawn.
 *
 * Everything here is LOCAL and COSMETIC — replay playback is client-side and the spectator camera
 * is never replicated. The object holds the player controller and camera seam WEAKLY.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSREPLAY_API URep_SpectatorController : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Enter spectator mode for the given local controller. Resolves the IRep_SpectatorCamera seam;
	 * if present, enters camera-driven framing, otherwise falls back to an engine spectator pawn.
	 * Returns false if no local controller is available. Re-entering while active is a no-op.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Spectator")
	bool EnterSpectator(APlayerController* LocalController);

	/** Leave spectator mode and restore the prior view target / pawn. Safe if not active (no-op). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Spectator")
	void ExitSpectator();

	/** True while spectator mode is active. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Replay|Spectator")
	bool IsSpectating() const { return bSpectating; }

	/**
	 * Focus the spectator on an actor (e.g. the player who triggered a marker). Forwards to the
	 * camera seam if it supports focusing; otherwise sets the engine view target to Target. No-op
	 * if not spectating or Target is null.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Spectator")
	void FocusOnActor(AActor* Target);

private:
	/** The local player controller we are spectating through. Weak — playback worlds tear down. */
	UPROPERTY(Transient)
	TWeakObjectPtr<APlayerController> Controller;

	/** Resolved camera seam (weak — it may be a world-scoped object). Null => engine fallback path. */
	TWeakInterfacePtr<IRep_SpectatorCamera> CameraSeam;

	/** Handle returned by the camera seam's EnterSpectatorCamera, passed back on exit. */
	FGuid CameraHandle;

	/** The spectator pawn spawned by the engine fallback (so we can restore/destroy it). */
	UPROPERTY(Transient)
	TWeakObjectPtr<APawn> FallbackPawn;

	/** The actor that was the view target before we entered, restored on exit. */
	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> PriorViewTarget;

	/** The pawn the controller possessed before we entered (engine fallback path), restored on exit. */
	UPROPERTY(Transient)
	TWeakObjectPtr<APawn> PriorPawn;

	/** True while active. */
	bool bSpectating = false;

	/** True when we used the engine-pawn fallback (no camera seam was registered). */
	bool bUsingFallbackPawn = false;

	/** Resolve the registered IRep_SpectatorCamera from the service locator, or an empty ptr. */
	void ResolveCameraSeam();

	/** Spawn + possess the fallback spectator pawn for free-fly. Returns false on failure. */
	bool EnterFallbackPawn();

	/** Destroy the fallback pawn and restore the prior pawn/view target. */
	void ExitFallbackPawn();
};
