// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Data/DPDataAsset.h"
#include "Audio_MixProfileDataAsset.generated.h"

class USoundSubmix;
class USoundClass;

/**
 * One submix volume override inside a mix profile: target submix + linear volume + fade time.
 *
 * The submix reference is SOFT so an unloaded mix-profile bank costs nothing until applied; the mix
 * controller loads it when the profile is pushed.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSAUDIO_API FAudio_SubmixOverride
{
	GENERATED_BODY()

	/** The submix whose output volume this entry drives via the AudioMixer submix API. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mix")
	TSoftObjectPtr<USoundSubmix> Submix;

	/** Target linear output volume for the submix while this profile is the active top-of-stack. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mix", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "2.0"))
	float Volume = 1.f;

	/** Seconds to interpolate to Volume when this profile becomes active (0 = instant). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mix", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "5.0"))
	float FadeTime = 0.5f;
};

/**
 * One ducking rule inside a mix profile: while active, scale a target audio CATEGORY's voice volume.
 *
 * This is the data-side counterpart to the sound manager's ducking: the manager applies the deepest
 * active duck per category. Categories are addressed by tag (child of DP.Audio.Category) and the
 * sound manager honours the rule across the tag hierarchy.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSAUDIO_API FAudio_DuckRule
{
	GENERATED_BODY()

	/** Category to duck while this profile is active (child of DP.Audio.Category). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mix", meta = (Categories = "DP.Audio.Category"))
	FGameplayTag TargetCategory;

	/**
	 * Linear volume the target category is scaled to while ducked (0 = silent, 1 = no ducking).
	 * <0 means "use the project default duck volume" from Audio_DeveloperSettings.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mix", meta = (ClampMin = "-1.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float DuckVolume = 0.4f;
};

/**
 * A data-driven mix snapshot: a named set of submix volume overrides plus duck rules that can be
 * pushed/popped by priority over the engine's AudioMixer.
 *
 * Subclass of UDP_DataAsset, so each profile is identified by its DataTag (child of DP.Audio.Mix)
 * and indexed by the data registry. The mix controller resolves a profile by tag, applies its submix
 * overrides through the AudioMixer submix API and hands its duck rules to the sound manager.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSAUDIO_API UAudio_MixProfileDataAsset : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/**
	 * Stack priority for this profile. When several profiles are pushed, the highest priority wins
	 * (ties broken by push order — last push wins). A pure designer ordering value.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mix", meta = (ClampMin = "0", UIMin = "0", UIMax = "100"))
	int32 Priority = 0;

	/** Submix output-volume overrides applied while this profile is the active top-of-stack. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mix")
	TArray<FAudio_SubmixOverride> SubmixOverrides;

	/** Category duck rules handed to the sound manager while this profile is active. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mix")
	TArray<FAudio_DuckRule> DuckRules;

	//~ Begin UDP_DataAsset
	/** Collapse all mix profiles into one asset-manager type bucket ("Audio_MixProfile"). */
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset

#if WITH_EDITOR
	//~ Begin UObject (editor validation)
	/** Flags submix overrides with no submix and duck rules whose category is outside the audio root. */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
	//~ End UObject
#endif
};
