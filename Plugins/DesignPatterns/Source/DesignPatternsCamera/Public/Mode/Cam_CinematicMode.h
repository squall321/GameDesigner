// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ScriptInterface.h"
#include "Mode/Cam_CameraMode.h"
#include "Mode/Cam_FramingSolver.h"
#include "Cam_CinematicMode.generated.h"

class ICam_TargetSource;

/**
 * Cinematic / framing camera mode.
 *
 * Uses FCam_FramingSolver to frame the view target with rule-of-thirds composition, optional dolly-zoom,
 * and (when bDualTargetFrame is set) lock-on dual-target framing: it resolves the player's current
 * lock-on through a TScriptInterface<ICam_TargetSource> (from the service locator under
 * Cam.Service.TargetSource), reads the locked target's FSeam_EntityId, resolves that id to a world
 * location via ISeam_EntityIdentity, and frames the midpoint of pawn + target so both stay on screen.
 *
 * It degrades cleanly to single-subject framing whenever no target source / no valid lock resolves, so
 * the mode is robust on dedicated servers, before the locator is populated, or when nothing is locked.
 * Pure data -> view; never touches APlayerCameraManager; cosmetic and never replicated.
 */
UCLASS(meta = (DisplayName = "Cinematic / Framing"))
class DESIGNPATTERNSCAMERA_API UCam_CinematicMode : public UCam_CameraMode
{
	GENERATED_BODY()

public:
	UCam_CinematicMode();

	virtual void UpdateView_Implementation(float DeltaTime, const FCam_ViewContext& Context, FCam_CameraView& OutView) override;
	virtual void OnEnterStack_Implementation(const FCam_ViewContext& Context) override;
	virtual void OnExitStack_Implementation() override;

protected:
	/** Framing parameters (composition, distance, dual padding, FOV, dolly-zoom). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cinematic")
	FCam_FramingParams Framing;

	/** When true the mode tries to frame the player + current lock-on target together. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cinematic")
	bool bDualTargetFrame = false;

	/**
	 * Positional lag (s) for the framed camera so it eases rather than snaps when the subject(s) move
	 * or the lock-on changes. 0 = rigid (instant cuts). Defensive default gives a smooth cinematic glide.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cinematic|Lag", meta = (ClampMin = "0.0", Units = "s"))
	float LocationLag = 0.25f;

	/** Rotational lag (s) for the look-at aim. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cinematic|Lag", meta = (ClampMin = "0.0", Units = "s"))
	float RotationLag = 0.2f;

private:
	/** Resolve (and cache) the lock-on target source from the service locator, or null. */
	TScriptInterface<ICam_TargetSource> ResolveTargetSource(const UObject* WorldContext);

	/** Resolve the locked target's world location via its id + ISeam_EntityIdentity. False if none. */
	bool TryGetLockedTargetLocation(const UObject* WorldContext, FVector& OutLocation);

	/** Cached lock-on target source seam (re-resolved if stale). */
	UPROPERTY(Transient)
	TScriptInterface<ICam_TargetSource> TargetSourceCache;

	/** Smoothed camera location, carried across frames for LocationLag. */
	UPROPERTY(Transient)
	FVector SmoothedLocation = FVector::ZeroVector;

	/** Smoothed look rotation, carried across frames for RotationLag. */
	UPROPERTY(Transient)
	FRotator SmoothedRotation = FRotator::ZeroRotator;

	/** Smoothed FOV (so dolly-zoom transitions ease). */
	UPROPERTY(Transient)
	float SmoothedFOV = 50.f;

	/** Whether the smoothed state has been seeded on a valid frame. */
	UPROPERTY(Transient)
	bool bSeeded = false;
};

/**
 * Transient externally-driven override mode used by the director to implement ISeam_CinematicCameraSink.
 *
 * It simply reports a directly-set world POV + FOV (e.g. a ULevelSequence-sampled transform), so a
 * cutscene can drive the local camera by Update each frame. It is pushed/popped by the director on its
 * OWN stack at a cinematic priority and the blend in/out is handled by the stack's normal weight easing
 * (the director seeds BlendInTime/BlendOutTime when it pushes). Pure data -> view; cosmetic.
 */
UCLASS(meta = (DisplayName = "Cinematic Override"))
class DESIGNPATTERNSCAMERA_API UCam_CinematicOverrideMode : public UCam_CameraMode
{
	GENERATED_BODY()

public:
	UCam_CinematicOverrideMode();

	virtual void UpdateView_Implementation(float DeltaTime, const FCam_ViewContext& Context, FCam_CameraView& OutView) override;

	/** Set the externally-driven POV/FOV. FOV <= 0 keeps the last/contextual FOV. */
	void SetOverride(const FTransform& InPOV, float InFOV);

	/** Set the blend times the director wants for this override (so the stack eases in/out). */
	void ConfigureBlend(float InBlendIn, float InBlendOut);

private:
	/** Externally-driven location. */
	UPROPERTY(Transient)
	FVector OverrideLocation = FVector::ZeroVector;

	/** Externally-driven rotation. */
	UPROPERTY(Transient)
	FRotator OverrideRotation = FRotator::ZeroRotator;

	/** Externally-driven FOV (<= 0 means "keep contextual FOV"). */
	UPROPERTY(Transient)
	float OverrideFOV = -1.f;

	/** Whether SetOverride has been called at least once. */
	UPROPERTY(Transient)
	bool bHasOverride = false;
};
