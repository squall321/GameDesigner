// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "UPlat_HapticSettings.generated.h"

class UPlat_HapticEffectSet;

/**
 * Project + user haptic configuration for the DesignPatternsPlatform module. Appears under
 * Project Settings -> Plugins -> Design Patterns Platform Haptics. Mirrors the Cam_DeveloperSettings
 * idiom (engine UDeveloperSettings, Config=Game, DefaultConfig, GetCategoryName "Plugins", static Get()).
 * Holds only the MECHANISM toggles/scalars; the actual effect rows are authored in a UPlat_HapticEffectSet.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Design Patterns Platform Haptics"))
class DESIGNPATTERNSPLATFORM_API UPlat_HapticSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPlat_HapticSettings();

	virtual FName GetCategoryName() const override { return FName("Plugins"); }

	/** Convenience accessor for the CDO. Never null in a configured project; null-checked by callers. */
	static const UPlat_HapticSettings* Get();

	/** Whether haptics start enabled. Players can toggle live via UPlat_HapticFeedbackSubsystem::SetHapticsEnabled. */
	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Haptics")
	bool bHapticsEnabledByDefault = true;

	/** Global intensity multiplier applied to every haptic on top of per-row / per-category scaling. */
	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Haptics", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MasterIntensity = 1.f;

	/**
	 * The default haptic bank the subsystem loads on startup when no bank is set explicitly. Soft so the
	 * setting does not force-load a bank into every cook that merely includes this module.
	 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Haptics")
	TSoftObjectPtr<UPlat_HapticEffectSet> DefaultEffectSet;

	/**
	 * Per-category intensity scalars, keyed by FPlat_HapticEffect::CategoryTag. A row whose category is
	 * present here has its intensity multiplied by the scalar (e.g. tune all UI haptics down). Missing
	 * category = scalar 1.0.
	 */
	UPROPERTY(EditAnywhere, Config, BlueprintReadOnly, Category = "Haptics")
	TMap<FGameplayTag, float> CategoryIntensityScalars;
};
