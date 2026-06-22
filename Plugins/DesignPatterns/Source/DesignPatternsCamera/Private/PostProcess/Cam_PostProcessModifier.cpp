// Copyright DesignPatterns plugin. All Rights Reserved.

#include "PostProcess/Cam_PostProcessModifier.h"

#include "Scene.h" // FPostProcessSettings

UCam_PostProcessModifier::UCam_PostProcessModifier()
{
}

void UCam_PostProcessModifier::SetDesiredPostProcess(const FCam_PostProcessSettings& In)
{
	Desired = In;
	bHasDesired = true;
}

void UCam_PostProcessModifier::ClearDesiredPostProcess()
{
	bHasDesired = false;
	Desired = FCam_PostProcessSettings();
}

void UCam_PostProcessModifier::ModifyPostProcess(float /*DeltaTime*/, float& PostProcessBlendWeight, FPostProcessSettings& PostProcessSettings)
{
	if (!bHasDesired)
	{
		// Contribute nothing: weight 0 so the manager ignores our (empty) settings.
		PostProcessBlendWeight = 0.f;
		return;
	}

	// Effective weight honours both the preset's authored BlendWeight and this modifier's Alpha
	// (the manager's enable/disable fade), so toggling the modifier eases the post-process in/out.
	const float EffectiveWeight = FMath::Clamp(Desired.BlendWeight, 0.f, 1.f) * FMath::Clamp(Alpha, 0.f, 1.f);
	PostProcessBlendWeight = EffectiveWeight;

	if (EffectiveWeight <= 0.f)
	{
		return;
	}

	// Depth of field (cinematic). Only enable when a focal distance is authored.
	if (Desired.DofFocalDistance > 0.f)
	{
		PostProcessSettings.bOverride_DepthOfFieldFocalDistance = true;
		PostProcessSettings.DepthOfFieldFocalDistance = Desired.DofFocalDistance;

		PostProcessSettings.bOverride_DepthOfFieldFstop = true;
		PostProcessSettings.DepthOfFieldFstop = FMath::Max(Desired.DofAperture, 0.01f);
	}

	// Vignette.
	if (Desired.VignetteIntensity > 0.f)
	{
		PostProcessSettings.bOverride_VignetteIntensity = true;
		PostProcessSettings.VignetteIntensity = FMath::Clamp(Desired.VignetteIntensity, 0.f, 1.f);
	}

	// Film grain (engine field name is FilmGrainIntensity in 5.x).
	if (Desired.GrainIntensity > 0.f)
	{
		PostProcessSettings.bOverride_FilmGrainIntensity = true;
		PostProcessSettings.FilmGrainIntensity = FMath::Clamp(Desired.GrainIntensity, 0.f, 1.f);
	}
}
