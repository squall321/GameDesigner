// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "Cam_ShakeProfile.generated.h"

class UCurveFloat;

/**
 * Advanced-shake DATA asset. It does NOT define a new shake class — it names a ShakeTag into the
 * existing UCam_CameraShakeLibrary (so the actual UCameraShakeBase is still authored there) and adds
 * the spatial / layering metadata an advanced director needs: a distance-falloff curve, inner/outer
 * radii, and an additive flag.
 *
 * ComputeScaleAtDistance returns the falloff multiplier [0,1] for a source at a given distance from the
 * shake epicenter: full inside InnerRadius, zero past OuterRadius, shaped by DistanceFalloff in between
 * (linear when the curve is null). All numbers are EditAnywhere — no magic numbers in code.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSCAMERA_API UCam_ShakeProfile : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	UCam_ShakeProfile();

	/** The library row (UCam_CameraShakeLibrary ShakeTag) this profile plays. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Shake")
	FGameplayTag ShakeTag;

	/**
	 * Optional curve mapping normalised distance [0,1] (InnerRadius..OuterRadius) -> scale [0,1].
	 * Null = linear falloff. Lets designers author an ease (e.g. fast drop near the edge).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Shake")
	TObjectPtr<UCurveFloat> DistanceFalloff = nullptr;

	/** Inside this radius (cm) the shake plays at full scale. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Shake", meta = (ClampMin = "0.0", Units = "cm"))
	float InnerRadius = 100.f;

	/** Past this radius (cm) the shake does not play (scale 0). Must be >= InnerRadius. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Shake", meta = (ClampMin = "0.0", Units = "cm"))
	float OuterRadius = 2000.f;

	/** When true this profile layers additively over any already-playing shake rather than replacing it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Shake")
	bool bAdditive = true;

	/**
	 * Compute the falloff multiplier [0,1] for a viewer at Distance (cm) from the epicenter.
	 * Full (1) at/below InnerRadius, 0 at/above OuterRadius, shaped by DistanceFalloff between them.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Camera|Shake")
	float ComputeScaleAtDistance(float Distance) const;

	//~ Begin UDP_DataAsset
	/** Groups all shake profiles into one asset-manager bucket. */
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset
};

/**
 * Route row mirroring FCam_ShakeBusRoute's shape but for advanced profiles: a bus/event channel tag ->
 * a soft UCam_ShakeProfile + an extra scale. Lets the advanced director subscribe to a channel and
 * read the shake epicenter from the broadcast payload.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSCAMERA_API FCam_ShakeProfileRoute
{
	GENERATED_BODY()

	/** Event/bus channel to route from (matched hierarchy-aware, like FCam_ShakeBusRoute::Channel). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Shake")
	FGameplayTag Channel;

	/** Profile to play when the channel fires. Soft so routes don't force-load every profile. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Shake")
	TSoftObjectPtr<UCam_ShakeProfile> Profile;

	/** Extra per-route scale multiplied on top of the falloff + accessibility scale. Clamped >= 0. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Camera|Shake", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float RouteScale = 1.f;

	FCam_ShakeProfileRoute() = default;
};
