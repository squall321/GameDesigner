// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Seam_HapticController.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_HapticController : public UInterface
{
	GENERATED_BODY()
};

/**
 * Haptic-feedback seam, owned by the Platform module's haptic subsystem
 * (UPlat_HapticFeedbackSubsystem). Gameplay, UI and combat systems trigger tag-keyed
 * haptics (controller force-feedback / rumble, mobile vibration) WITHOUT depending on the
 * Platform module: they resolve the provider object from the service locator under
 * DP.Service.Platform.Haptics and wrap it in a TScriptInterface<ISeam_HapticController>.
 *
 * Haptics are a purely-cosmetic, LOCAL effect — they are never replicated. A gameplay event
 * that is already replicated (a hit landing, a pickup) drives the local haptic on each client.
 * Held weakly by consumers and a no-op when unset, so the framework never requires a haptics
 * implementation to be present.
 */
class DESIGNPATTERNSSEAMS_API ISeam_HapticController
{
	GENERATED_BODY()

public:
	/**
	 * Play the haptic effect mapped to EffectTag, optionally scaled. Scale multiplies motor
	 * intensities / vibration amplitude in [0,1] after the implementation's own settings scalar.
	 * A no-op when haptics are disabled or the tag is unmapped.
	 *
	 * @param EffectTag  Identity of the effect row in the active haptic bank.
	 * @param Scale      Caller intensity multiplier (1.0 = the bank's authored intensity).
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Platform")
	void PlayHapticByTag(FGameplayTag EffectTag, float Scale);

	/** Stop any currently-playing haptics (force-feedback + motor drive). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Platform")
	void StopHaptics();

	/** True when haptics are currently enabled (device-capable AND not turned off in settings). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Platform")
	bool AreHapticsEnabled() const;
};
