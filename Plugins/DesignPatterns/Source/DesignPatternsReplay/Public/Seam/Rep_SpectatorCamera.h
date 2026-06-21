// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Rep_SpectatorCamera.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class URep_SpectatorCamera : public UInterface
{
	GENERATED_BODY()
};

/**
 * Seam between the replay spectator and the project's camera system.
 *
 * The replay spectator/free-cam (URep_SpectatorController) wants to drive a cinematic free-flying
 * camera during playback, but it must NOT hard-include the DesignPatternsCamera module (a sibling
 * high-level module). So instead it resolves THIS seam from the service locator under
 * Rep_NativeTags::Service_SpectatorCamera. The DesignPatternsCamera module (or the game) registers
 * an adapter that forwards to its ICam_CameraModeProvider mode stack — typically by pushing a
 * Cam.Mode.Fixed / free-fly mode while spectating and popping it when spectating ends.
 *
 * When NOTHING is registered, the spectator degrades gracefully to a documented inert default:
 * it possesses an engine ASpectatorPawn (or repositions the existing view target) directly via the
 * player controller, so free-cam still works without the Camera module present.
 *
 * All operations are LOCAL and COSMETIC — replay playback is a client-side concept and the camera
 * is never replicated.
 */
class DESIGNPATTERNSREPLAY_API IRep_SpectatorCamera
{
	GENERATED_BODY()

public:
	/**
	 * Enter spectator/free-cam framing for the given local player controller. The implementer
	 * typically pushes a free-fly camera mode and returns an opaque handle the spectator passes
	 * back to ExitSpectatorCamera. Returns an invalid FGuid if the camera could not be engaged
	 * (the spectator then uses its inert engine-pawn fallback).
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Replay|Spectator")
	FGuid EnterSpectatorCamera(APlayerController* LocalController);

	/** Release a previously-entered spectator framing. Safe with an invalid/expired handle (no-op). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Replay|Spectator")
	void ExitSpectatorCamera(APlayerController* LocalController, FGuid Handle);

	/**
	 * Optionally focus the spectator camera on an actor (e.g. follow the player who scored). The
	 * implementer may ignore this (return false) if it only supports manual free-fly.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Replay|Spectator")
	bool FocusOnActor(APlayerController* LocalController, AActor* Target);
};
