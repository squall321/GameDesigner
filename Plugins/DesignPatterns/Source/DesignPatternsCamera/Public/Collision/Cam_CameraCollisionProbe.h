// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Engine/EngineTypes.h"
#include "Mode/Cam_CameraMode.h"
#include "Identity/Seam_EntityId.h"
#include "Cam_CameraCollisionProbe.generated.h"

/**
 * Cosmetic occlusion payload the director optionally broadcasts on Cam.Bus.Occlusion when the probe
 * reports the view target is occluded. Carried inside an FInstancedStruct on the bus (never plain-
 * replicated). Project-side material fade (dither/translucency) listens here; the probe never fades.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSCAMERA_API FCam_OcclusionEvent
{
	GENERATED_BODY()

	/** Eased occlusion strength in [0,1] (from FCam_CollisionResult::OcclusionAlpha). */
	UPROPERTY(BlueprintReadOnly, Category = "Camera|Collision")
	float OcclusionAlpha = 0.f;

	/** Stable id of the occluded view target, when it exposes one (else invalid). */
	UPROPERTY(BlueprintReadOnly, Category = "Camera|Collision")
	FSeam_EntityId TargetId;

	FCam_OcclusionEvent() = default;
};

/**
 * Pure result of one collision/occlusion probe pass. Transient, never replicated. Lets the director
 * read the facts of the pull-in / occlusion test and optionally broadcast a cosmetic occlusion event
 * for project-side material fade. The probe never fades meshes or touches APlayerCameraManager itself.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSCAMERA_API FCam_CollisionResult
{
	GENERATED_BODY()

	/** True when the camera was pulled in from its desired location to avoid clipping geometry. */
	UPROPERTY(BlueprintReadOnly, Category = "Camera|Collision")
	bool bCameraPulledIn = false;

	/** True when the direct line between camera and pivot (the view target) is blocked. */
	UPROPERTY(BlueprintReadOnly, Category = "Camera|Collision")
	bool bTargetOccluded = false;

	/**
	 * Normalised occlusion strength in [0,1]: 0 = clear line to the target, 1 = fully occluded for
	 * at least OcclusionFullSeconds. Eased over time so project-side fade is smooth. Cosmetic only.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Camera|Collision")
	float OcclusionAlpha = 0.f;

	/** Fraction along the desired camera ray at which the first blocking hit occurred (1 = no hit). */
	UPROPERTY(BlueprintReadOnly, Category = "Camera|Collision")
	float HitFraction = 1.f;

	FCam_CollisionResult() = default;
};

/**
 * Strategy base for a post-evaluation camera collision/occlusion stage.
 *
 * Runs AFTER UCam_CameraModeStack::EvaluateStack produces the blended FCam_CameraView (so it corrects
 * whatever mode/blend is live) and BEFORE the director feeds the modifier. It is the composable,
 * data-authored equivalent of a USpringArmComponent's collision test, but kept PURE: given the pivot,
 * the desired view and a delta time it returns an adjusted view + an FCam_CollisionResult. It NEVER
 * touches APlayerCameraManager, never sets a view target, and never fades materials.
 *
 * EditInlineNew/Blueprintable so a director composes a concrete probe inline and tunes its knobs with
 * no code. Abstract: ship concrete subclasses (UCam_SweptSphereCollisionProbe).
 *
 * LOCAL / COSMETIC: instanced subobject of the director, transient, never replicated.
 */
UCLASS(EditInlineNew, Blueprintable, Abstract, CollapseCategories)
class DESIGNPATTERNSCAMERA_API UCam_CameraCollisionProbe : public UObject
{
	GENERATED_BODY()

public:
	UCam_CameraCollisionProbe();

	/**
	 * Resolve collision/occlusion for this frame.
	 * @param WorldContext   Any object able to resolve the world (the director). Must be valid.
	 * @param Context        Read-only per-frame inputs (pivot, view target, previous view).
	 * @param DesiredView    The blended view the stack produced (the camera's wanted POV).
	 * @param DeltaTime      Frame delta (s), used only for pull-in/occlusion easing.
	 * @param OutAdjustedView  Filled with the (possibly pulled-in) view to apply.
	 * @param OutResult      Filled with the facts of the test (pull-in, occlusion alpha, hit fraction).
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Camera|Collision")
	void ResolveCollision(const UObject* WorldContext, const FCam_ViewContext& Context,
		const FCam_CameraView& DesiredView, float DeltaTime,
		FCam_CameraView& OutAdjustedView, FCam_CollisionResult& OutResult);
	virtual void ResolveCollision_Implementation(const UObject* WorldContext, const FCam_ViewContext& Context,
		const FCam_CameraView& DesiredView, float DeltaTime,
		FCam_CameraView& OutAdjustedView, FCam_CollisionResult& OutResult);

	/** Reset any transient smoothing state (e.g. when the camera teleports). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Camera|Collision")
	virtual void ResetProbeState() {}
};

/**
 * Concrete spring-arm-style probe: sweeps a sphere from the pivot toward the desired camera location
 * on ProbeChannel and pulls the camera in to the first blocking hit (clamped to MinPullInDistance),
 * easing the radial distance with separate pull-in / pull-out lag so the camera snaps in fast on a
 * sudden wall but eases back out gently. Independently traces pivot->camera for an occlusion alpha
 * used by project-side fade. All tuning lives on UPROPERTYs (no magic numbers in the math).
 */
UCLASS(meta = (DisplayName = "Swept Sphere Collision Probe"))
class DESIGNPATTERNSCAMERA_API UCam_SweptSphereCollisionProbe : public UCam_CameraCollisionProbe
{
	GENERATED_BODY()

public:
	UCam_SweptSphereCollisionProbe();

	virtual void ResolveCollision_Implementation(const UObject* WorldContext, const FCam_ViewContext& Context,
		const FCam_CameraView& DesiredView, float DeltaTime,
		FCam_CameraView& OutAdjustedView, FCam_CollisionResult& OutResult) override;

	virtual void ResetProbeState() override;

protected:
	/** Radius (cm) of the swept sphere; bigger keeps the camera further off near geometry. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision", meta = (ClampMin = "0.0", Units = "cm"))
	float ProbeRadius = 12.f;

	/** Pull-IN lag time-constant (s). Small = snaps in quickly on a wall. 0 = rigid. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision", meta = (ClampMin = "0.0", Units = "s"))
	float PullInLag = 0.05f;

	/** Pull-OUT lag time-constant (s). Larger = eases back out gently when the wall clears. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision", meta = (ClampMin = "0.0", Units = "s"))
	float PullOutLag = 0.2f;

	/** Collision channel the sweep tests against. ECC_Camera by default (engine camera-collision channel). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision")
	TEnumAsByte<ECollisionChannel> ProbeChannel = ECC_Camera;

	/** Minimum distance (cm) from the pivot the camera is ever pulled to, so it never ends up inside the pivot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision", meta = (ClampMin = "0.0", Units = "cm"))
	float MinPullInDistance = 20.f;

	/** When true, the probe also traces pivot->camera to compute an occlusion alpha for project-side fade. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Occlusion")
	bool bComputeOcclusion = true;

	/** Seconds the line must stay blocked for OcclusionAlpha to reach 1 (and clear to reach 0). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Occlusion", meta = (ClampMin = "0.0", Units = "s"))
	float OcclusionFullSeconds = 0.25f;

private:
	/** Smoothed radial distance from pivot to camera (cm), carried across frames for the lag. */
	UPROPERTY(Transient)
	float SmoothedDistance = 0.f;

	/** Eased occlusion alpha carried across frames. */
	UPROPERTY(Transient)
	float SmoothedOcclusion = 0.f;

	/** Whether SmoothedDistance has been seeded on a valid frame. */
	UPROPERTY(Transient)
	bool bSeeded = false;
};
