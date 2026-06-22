// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Seam_CinematicCameraSink.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_CinematicCameraSink : public UInterface
{
	GENERATED_BODY()
};

/**
 * Cinematic-override seam over "something that owns the local camera and can be temporarily driven by
 * an externally-authored point of view" (a ULevelSequence-sampled cutscene POV, a scripted framing).
 *
 * The Camera module's UCam_CameraDirectorComponent implements this so a Narrative cutscene / sequence
 * director can blend the local camera TO a sampled transform+FOV, drive it each frame, then blend it
 * BACK to gameplay — WITHOUT the producer ever including the Camera module's concrete director type.
 * The implementer pushes a transient override on its OWN mode stack honouring the requested blend
 * times, so the engine camera pipeline (UCameraModifier, post-process, shakes) keeps cooperating.
 *
 * RESOLUTION: the producer resolves a TScriptInterface<ISeam_CinematicCameraSink> off the locally-
 * controlled actor (Controller / Pawn -> FindComponentByInterface) or via the service locator under a
 * stable key (Cam.Service.CinematicSink in the Camera module's native tags).
 *
 * LOCAL / COSMETIC: like every camera operation this is per-client and NEVER replicated. A cutscene is
 * triggered by already-replicated narrative state and runs the local override on each machine. The
 * handle returned identifies one override so a caller releases exactly its own without disturbing a
 * second concurrent push.
 *
 * House style: BlueprintNativeEvent (project/UI-facing bridge), with a .cpp of fail-safe no-op default
 * bodies (an unset/non-implementing sink ignores the override entirely). Uses only Core/CoreUObject and
 * Engine's FTransform/FGuid already available to the leaf Seams module — no new Seams Build.cs dep.
 */
class DESIGNPATTERNSSEAMS_API ISeam_CinematicCameraSink
{
	GENERATED_BODY()

public:
	/**
	 * Begin a cinematic camera override, blending the local camera toward POV/FOV over BlendInTime.
	 *
	 * @param POV          Desired camera world transform (location + rotation; scale ignored).
	 * @param FOV          Desired horizontal field of view in degrees. Values <= 0 keep the current FOV.
	 * @param BlendInTime  Seconds to ease in. 0 snaps. Clamped >= 0 by the implementer.
	 * @return An opaque handle identifying this override; pass it to Update/EndCinematicOverride.
	 *         Returns an invalid FGuid when no camera/director is available locally (e.g. dedicated server).
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Camera")
	FGuid BeginCinematicOverride(FTransform POV, float FOV, float BlendInTime);

	/**
	 * Update the live POV/FOV of an active override (call each frame while sampling a sequence).
	 * Safe to call with an invalid/already-ended handle (no-op).
	 *
	 * @param Handle  The handle returned by BeginCinematicOverride.
	 * @param POV     New camera world transform for this frame.
	 * @param FOV     New horizontal FOV in degrees; values <= 0 keep the current FOV.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Camera")
	void UpdateCinematicOverride(FGuid Handle, FTransform POV, float FOV);

	/**
	 * End a cinematic override, blending back to gameplay framing over BlendOutTime.
	 * Safe to call with an invalid/already-ended handle (no-op).
	 *
	 * @param Handle        The handle returned by BeginCinematicOverride.
	 * @param BlendOutTime  Seconds to ease back out. 0 snaps. Clamped >= 0 by the implementer.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Camera")
	void EndCinematicOverride(FGuid Handle, float BlendOutTime);
};
