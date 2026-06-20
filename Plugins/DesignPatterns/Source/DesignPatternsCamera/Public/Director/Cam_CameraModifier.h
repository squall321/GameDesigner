// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraModifier.h"
#include "Mode/Cam_CameraMode.h"
#include "Cam_CameraModifier.generated.h"

/**
 * The single point at which this module touches the engine camera pipeline. A UCameraModifier
 * registered on the local APlayerCameraManager that overwrites the manager's computed POV with the
 * director's blended view. Using a modifier (rather than a custom ACameraActor or fighting
 * APlayerCameraManager::UpdateViewTarget) means we cooperate with the engine: other modifiers (shakes,
 * post-process) still run, and the manager owns the camera component / view-target lifecycle.
 *
 * The director sets DesiredView and bHasDesiredView every tick BEFORE the manager updates; on a frame
 * with no desired view the modifier passes the manager's POV through unchanged. The modifier itself is
 * stateless and local/cosmetic — never replicated.
 */
UCLASS(NotBlueprintable)
class DESIGNPATTERNSCAMERA_API UCam_CameraModifier : public UCameraModifier
{
	GENERATED_BODY()

public:
	UCam_CameraModifier();

	//~ Begin UCameraModifier
	/**
	 * Cross-version-stable modifier hook. Replaces the incoming POV with DesiredView when one is set,
	 * honouring this modifier's Alpha (so the manager's enable/disable fade still applies). Returns
	 * false so subsequent modifiers continue to run.
	 */
	virtual bool ModifyCamera(float DeltaTime, FVector ViewLocation, FRotator ViewRotation, float FOV,
		FVector& NewViewLocation, FRotator& NewViewRotation, float& NewFOV) override;
	//~ End UCameraModifier

	/** Push the director's blended view for this frame. Marks a desired view as present. */
	void SetDesiredView(const FCam_CameraView& InView);

	/** Clear the desired view so the modifier passes the manager POV through (e.g. director idle). */
	void ClearDesiredView();

	/** True if a desired view was set since the last clear. */
	bool HasDesiredView() const { return bHasDesiredView; }

private:
	/** The director's blended view to apply this frame. */
	UPROPERTY(Transient)
	FCam_CameraView DesiredView;

	/** Whether DesiredView is valid for this frame. */
	UPROPERTY(Transient)
	bool bHasDesiredView = false;
};
