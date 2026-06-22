// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Capability/UPlat_DeviceCapabilitySubsystem.h"
#include "Data/DPDataAsset.h"
#include "UPlat_ScalabilityTypes.generated.h"

/**
 * A per-tier scalability recipe covering ALL ten engine scalability groups plus dynamic-resolution
 * screen-percentage bounds. Keyed by the real EPlat_PerfTier. Every number is data — there are no
 * magic quality numbers in C++; the [0,4] clamps mirror the engine's quality-level range.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSPLATFORM_API FPlat_ScalabilityBucket
{
	GENERATED_BODY()

	/** The performance tier this bucket applies to. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform|Scalability")
	EPlat_PerfTier Tier = EPlat_PerfTier::High;

	// ---- The ten engine scalability groups (sg.* quality levels, 0..4 = Low..Cinematic) ----

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform|Scalability", meta = (ClampMin = "0", ClampMax = "4"))
	int32 ViewDistance = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform|Scalability", meta = (ClampMin = "0", ClampMax = "4"))
	int32 AntiAliasing = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform|Scalability", meta = (ClampMin = "0", ClampMax = "4"))
	int32 Shadow = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform|Scalability", meta = (ClampMin = "0", ClampMax = "4"))
	int32 GlobalIllumination = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform|Scalability", meta = (ClampMin = "0", ClampMax = "4"))
	int32 Reflection = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform|Scalability", meta = (ClampMin = "0", ClampMax = "4"))
	int32 PostProcess = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform|Scalability", meta = (ClampMin = "0", ClampMax = "4"))
	int32 Texture = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform|Scalability", meta = (ClampMin = "0", ClampMax = "4"))
	int32 Effects = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform|Scalability", meta = (ClampMin = "0", ClampMax = "4"))
	int32 Foliage = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform|Scalability", meta = (ClampMin = "0", ClampMax = "4"))
	int32 Shading = 2;

	// ---- Dynamic resolution ----

	/** Whether dynamic resolution is enabled for this tier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform|Scalability|DynRes")
	bool bEnableDynamicResolution = true;

	/** Lower bound of the dynamic-resolution screen percentage [0.25, 1.0]. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform|Scalability|DynRes", meta = (ClampMin = "0.25", ClampMax = "1.0"))
	float DynResMinScreenPercentage = 0.6f;

	/** Upper bound of the dynamic-resolution screen percentage [0.25, 1.0]. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform|Scalability|DynRes", meta = (ClampMin = "0.25", ClampMax = "1.0"))
	float DynResMaxScreenPercentage = 1.f;
};

/**
 * Data-driven set of FPlat_ScalabilityBucket rows keyed by EPlat_PerfTier, registered through the core
 * data registry by its inherited DataTag. A project ships one profile (with a row per tier); there are
 * no hard-coded quality numbers in C++.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSPLATFORM_API UPlat_ScalabilityProfile : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/** The per-tier buckets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform|Scalability")
	TArray<FPlat_ScalabilityBucket> Buckets;

	/**
	 * Find the bucket for a tier.
	 * @return True if a matching bucket was found and copied into Out.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Platform|Scalability")
	bool FindBucket(EPlat_PerfTier Tier, FPlat_ScalabilityBucket& Out) const;

	//~ Begin UDP_DataAsset
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset
};
