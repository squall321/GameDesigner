// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Data/DPDataAsset.h"
#include "UPlat_HapticTypes.generated.h"

class UForceFeedbackEffect;

/**
 * How a haptic row is delivered. A row is data-authored, so a designer chooses whether the effect is a
 * full engine UForceFeedbackEffect curve asset or a simple timed two-motor drive (handy for quick
 * gamepad bumps and for mobile vibration, which has no curve concept).
 */
UENUM(BlueprintType)
enum class EPlat_HapticMode : uint8
{
	/** Play a UForceFeedbackEffect asset through the player controller's force-feedback system. */
	ForceFeedbackAsset	UMETA(DisplayName = "Force Feedback Asset"),

	/** Drive the gamepad's large/small motors at fixed intensities for DurationSeconds. */
	MotorValues			UMETA(DisplayName = "Motor Values")
};

/**
 * One tag-keyed haptic effect row. Every tunable is data — there are no magic numbers in code; the
 * documented defaults are defensive fallbacks so a half-authored row still does something sane.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSPLATFORM_API FPlat_HapticEffect
{
	GENERATED_BODY()

	/** Identity of this effect; callers play by tag. Anchor under DP.Data.Platform.Haptic.* in project tags. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform|Haptics")
	FGameplayTag EffectTag;

	/** How this row plays (asset curve vs raw motor values). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform|Haptics")
	EPlat_HapticMode Mode = EPlat_HapticMode::ForceFeedbackAsset;

	/** The force-feedback curve asset to play when Mode == ForceFeedbackAsset. Soft so the bank is cook-light. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform|Haptics", meta = (EditCondition = "Mode == EPlat_HapticMode::ForceFeedbackAsset"))
	TSoftObjectPtr<UForceFeedbackEffect> ForceFeedback;

	/** Large (low-frequency) motor intensity [0,1] when Mode == MotorValues. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform|Haptics", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "Mode == EPlat_HapticMode::MotorValues"))
	float LargeMotor = 0.5f;

	/** Small (high-frequency) motor intensity [0,1] when Mode == MotorValues. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform|Haptics", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "Mode == EPlat_HapticMode::MotorValues"))
	float SmallMotor = 0.5f;

	/** How long the motor drive / mobile vibration lasts (seconds). Ignored for looping FF assets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform|Haptics", meta = (ClampMin = "0.0"))
	float DurationSeconds = 0.2f;

	/** Mobile (FPlatformMisc) vibration amplitude [0,1] used on touch devices that support vibration. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform|Haptics", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MobileVibrationIntensity = 0.6f;

	/**
	 * Optional category tag used to look up a per-category intensity scalar in settings (e.g. scale all
	 * "UI" haptics down without touching gameplay haptics). Invalid = no category scaling.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform|Haptics")
	FGameplayTag CategoryTag;

	bool IsValidRow() const { return EffectTag.IsValid(); }
};

/**
 * Tag-indexed bank of haptic effect rows, registered through the core data registry by its inherited
 * DataTag. A project ships one or more banks (e.g. a default bank + per-genre overrides) and the haptic
 * subsystem resolves a tag against the active bank.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSPLATFORM_API UPlat_HapticEffectSet : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/** The effect rows in this bank. Lookups are linear over a small list (banks are tens of rows). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Platform|Haptics")
	TArray<FPlat_HapticEffect> Effects;

	/**
	 * Find the row for an effect tag. Exact match first; if none, the closest parent-tag match is used so
	 * a hierarchical effect tag (DP.Data.Platform.Haptic.Hit.Heavy) can fall back to a coarser row.
	 * @return True if a row was found and copied into Out.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Platform|Haptics")
	bool FindEffect(FGameplayTag EffectTag, FPlat_HapticEffect& Out) const;

	//~ Begin UDP_DataAsset
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset
};
