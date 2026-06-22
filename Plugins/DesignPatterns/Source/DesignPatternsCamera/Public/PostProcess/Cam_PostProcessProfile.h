// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "Cam_PostProcessProfile.generated.h"

/**
 * Designer DOF/vignette/grain post-process preset. The transient runtime payload the director resolves
 * for the active mode and feeds to UCam_PostProcessModifier. A small, explicit subset of the engine's
 * FPostProcessSettings — the modifier maps these onto the manager's FPostProcessSettings with
 * BlendWeight, so we stay narrow (no giant settings struct) while still wrapping the engine pipeline.
 *
 * All fields are designer-authored (no magic numbers in code). Cosmetic / local; never replicated.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSCAMERA_API FCam_PostProcessSettings
{
	GENERATED_BODY()

	/** Depth-of-field focal distance (cm). 0 disables DOF for this preset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|PostProcess", meta = (ClampMin = "0.0", Units = "cm"))
	float DofFocalDistance = 0.f;

	/** Depth-of-field aperture (f-stop). Lower = shallower depth of field. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|PostProcess", meta = (ClampMin = "0.0"))
	float DofAperture = 4.f;

	/** Vignette intensity [0,1]. 0 disables vignette for this preset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|PostProcess", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float VignetteIntensity = 0.f;

	/** Film grain intensity [0,1]. 0 disables grain for this preset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|PostProcess", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float GrainIntensity = 0.f;

	/** Overall blend weight [0,1] this preset is applied at. 0 = no effect, 1 = fully applied. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|PostProcess", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BlendWeight = 1.f;

	FCam_PostProcessSettings() = default;
};

/**
 * One route row: an active-mode tag -> its post-process settings. The most specific (deepest) matching
 * ModeTag wins, mirroring the shake library's hierarchy-aware lookup.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSCAMERA_API FCam_PostProcessRoute
{
	GENERATED_BODY()

	/** The active camera-mode tag this route applies to (e.g. Cam.Mode.Cinematic). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|PostProcess", meta = (Categories = "Cam.Mode"))
	FGameplayTag ModeTag;

	/** The post-process settings to apply while that mode is active. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|PostProcess")
	FCam_PostProcessSettings Settings;

	FCam_PostProcessRoute() = default;
};

/**
 * Data asset mapping active mode tag -> post-process settings, so the director resolves DOF/vignette/
 * grain by GetActiveModeTag with no magic numbers in code. Identified by DataTag (UDP_DataAsset) so the
 * active profile is resolvable through the data registry / project settings.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSCAMERA_API UCam_PostProcessProfile : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	UCam_PostProcessProfile();

	/** Routes from active-mode tag to post-process settings. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|PostProcess")
	TArray<FCam_PostProcessRoute> Routes;

	/**
	 * Resolve the settings for an active mode tag using hierarchy-aware matching (most specific wins).
	 * @param ModeTag   The director's GetActiveModeTag().
	 * @param OutSettings  Filled with the matched settings.
	 * @return true if a route matched (OutSettings valid), false otherwise (no post-process).
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Camera|PostProcess")
	bool ResolveForMode(FGameplayTag ModeTag, FCam_PostProcessSettings& OutSettings) const;

	//~ Begin UDP_DataAsset
	/** Groups all post-process profiles into one asset-manager bucket. */
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset
};
