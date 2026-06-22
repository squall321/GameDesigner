// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Mode/Cam_CameraMode.h"
#include "Cam_SplineRailMode.generated.h"

class USplineComponent;

/**
 * Rail / spline-follow camera mode.
 *
 * Resolves a USplineComponent (Engine) on an actor tagged RailActorTag when it enters the stack, then
 * each frame places the camera at the point on the spline nearest the projected pivot (so the camera
 * "rides the rail" as the subject moves along it), easing its distance-along-spline with DistanceLag.
 * Optionally looks at the pivot (plus LookAtOffset) so the subject stays framed while the camera is
 * constrained to the authored path — classic for sidescroller follow-cams, on-rails sequences, and
 * scenic establishing moves. Pure data -> view: it never touches APlayerCameraManager.
 *
 * The rail is held as a TWeakObjectPtr and re-resolved if it goes stale (e.g. the rail actor streams
 * out and back). When no rail resolves the mode degrades gracefully to a fixed view above the pivot.
 */
UCLASS(meta = (DisplayName = "Spline Rail"))
class DESIGNPATTERNSCAMERA_API UCam_SplineRailMode : public UCam_CameraMode
{
	GENERATED_BODY()

public:
	UCam_SplineRailMode();

	virtual void UpdateView_Implementation(float DeltaTime, const FCam_ViewContext& Context, FCam_CameraView& OutView) override;
	virtual void OnEnterStack_Implementation(const FCam_ViewContext& Context) override;
	virtual void OnExitStack_Implementation() override;

protected:
	/** Actor tag identifying the actor that owns the rail USplineComponent. Resolved against the world. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rail")
	FName RailActorTag = NAME_None;

	/** Distance-along-spline lag time-constant (s) so the camera eases along the rail. 0 = rigid. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rail", meta = (ClampMin = "0.0", Units = "s"))
	float DistanceLag = 0.2f;

	/** If true the camera aims at the (offset) pivot; if false it uses the spline tangent as look dir. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rail")
	bool bLookAtPivot = true;

	/** Offset added to the pivot when computing the look-at target (e.g. raise to head height). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rail", meta = (EditCondition = "bLookAtPivot"))
	FVector LookAtOffset = FVector(0.f, 0.f, 40.f);

	/** Field of view (deg) this mode requests. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rail", meta = (ClampMin = "5.0", ClampMax = "170.0", Units = "deg"))
	float FieldOfView = 60.f;

	/** Rotational lag (s) applied to the look direction so aim eases rather than snaps. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rail", meta = (ClampMin = "0.0", Units = "s"))
	float RotationLag = 0.1f;

	/** Fallback height (cm) above the pivot used when no rail resolves, so the mode is never broken. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rail|Fallback", meta = (ClampMin = "0.0", Units = "cm"))
	float FallbackHeight = 300.f;

private:
	/** Resolve (and cache) the rail spline from the first actor tagged RailActorTag in the world. */
	USplineComponent* ResolveRail(const UObject* WorldContext);

	/** The resolved rail spline. Weak — never owns it; re-resolved if stale. */
	UPROPERTY(Transient)
	TWeakObjectPtr<USplineComponent> Rail;

	/** Smoothed input-key distance along the spline (in spline "key" units), carried across frames. */
	UPROPERTY(Transient)
	float SmoothedDistance = 0.f;

	/** Smoothed look rotation, carried across frames for RotationLag. */
	UPROPERTY(Transient)
	FRotator SmoothedRotation = FRotator::ZeroRotator;

	/** Whether the smoothed state has been seeded on a valid frame. */
	UPROPERTY(Transient)
	bool bSeeded = false;
};
