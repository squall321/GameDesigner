// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraModifier.h"
#include "PostProcess/Cam_PostProcessProfile.h"
#include "Cam_PostProcessModifier.generated.h"

/**
 * Second engine camera modifier the director installs (alongside the view-driving UCam_CameraModifier).
 *
 * This one overrides ModifyPostProcess (NOT ModifyCamera — that hook is owned by the view modifier) to
 * blend a resolved FCam_PostProcessSettings into the manager's FPostProcessSettings with a blend weight.
 * Using the engine modifier hook means we cooperate with the rest of the post-process pipeline rather
 * than fighting it, and the manager owns lifetime.
 *
 * The director resolves the active mode's post-process preset (via UCam_PostProcessProfile) and calls
 * SetDesiredPostProcess each tick; on a frame with no desired preset the modifier contributes nothing.
 * Stateless beyond Desired. LOCAL / COSMETIC: Transient, never replicated.
 */
UCLASS(NotBlueprintable)
class DESIGNPATTERNSCAMERA_API UCam_PostProcessModifier : public UCameraModifier
{
	GENERATED_BODY()

public:
	UCam_PostProcessModifier();

	//~ Begin UCameraModifier
	/**
	 * Blend the desired DOF/vignette/grain into PostProcessSettings, honouring this modifier's Alpha and
	 * the preset's BlendWeight. Contributes nothing when no desired preset is set.
	 */
	virtual void ModifyPostProcess(float DeltaTime, float& PostProcessBlendWeight, FPostProcessSettings& PostProcessSettings) override;
	//~ End UCameraModifier

	/** Set the desired post-process preset for subsequent frames. Marks a preset as present. */
	void SetDesiredPostProcess(const FCam_PostProcessSettings& In);

	/** Clear the desired preset so the modifier contributes nothing. */
	void ClearDesiredPostProcess();

	/** True if a desired preset is set. */
	bool HasDesiredPostProcess() const { return bHasDesired; }

private:
	/** The desired preset to apply this frame. */
	UPROPERTY(Transient)
	FCam_PostProcessSettings Desired;

	/** Whether Desired is valid for this frame. */
	UPROPERTY(Transient)
	bool bHasDesired = false;
};
