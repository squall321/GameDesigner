// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Loc/Seam_AccessibilityTypes.h"
#include "Seam_AccessibilityConsumer.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_AccessibilityConsumer : public UInterface
{
	GENERATED_BODY()
};

/**
 * Implemented by any system that reacts to accessibility options (UI scale, colorblind palette, subtitle
 * settings, shake scale). The Localization/Accessibility subsystem pushes the current options on register
 * and on every change. Consumers (UI/Camera/HUD) implement this WITHOUT including the Localization module —
 * the seam and its option struct both live in Seams.
 */
class DESIGNPATTERNSSEAMS_API ISeam_AccessibilityConsumer
{
	GENERATED_BODY()

public:
	/** Called with the current options on register and whenever any option changes. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Accessibility")
	void OnAccessibilityOptionsChanged(const FSeam_AccessibilityOptions& Options);
};
