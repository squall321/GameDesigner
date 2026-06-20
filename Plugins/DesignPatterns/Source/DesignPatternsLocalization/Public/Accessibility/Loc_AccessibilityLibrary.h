// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Loc/Seam_AccessibilityTypes.h"
#include "Loc_AccessibilityLibrary.generated.h"

/**
 * Stateless, world-context helpers that let any UI/Camera/HUD code consume the current accessibility
 * options WITHOUT taking a hard dependency on the accessibility subsystem's concrete type or caching
 * its own copy. Every accessor null-safely falls back to sane defaults (the seam struct's defaults)
 * when no subsystem is reachable, so callers stay correct even if the Localization module is removed.
 */
UCLASS()
class DESIGNPATTERNSLOCALIZATION_API ULoc_AccessibilityLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Fetch the current accessibility options for the given world context. If no accessibility subsystem
	 * exists (module removed / early boot), returns a default-constructed FSeam_AccessibilityOptions so
	 * callers always get a usable value.
	 */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Accessibility", meta = (WorldContext = "WorldContextObject"))
	static FSeam_AccessibilityOptions GetAccessibilityOptions(const UObject* WorldContextObject);

	/**
	 * Remap a color for the active (or an explicitly supplied) colorblind mode using a daltonization-style
	 * correction. With ESeam_ColorblindMode::None this is the identity. The remap operates in linear space
	 * and preserves alpha. This is a perceptual-aid approximation (not a clinical simulation) intended to
	 * increase hue separation for the three common dichromacies.
	 */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Accessibility")
	static FLinearColor ApplyColorblindToColor(const FLinearColor& InColor, ESeam_ColorblindMode Mode);

	/**
	 * Convenience overload that reads the current mode from the subsystem for the given world context,
	 * then remaps. Equivalent to ApplyColorblindToColor(InColor, GetAccessibilityOptions(WCO).ColorblindMode).
	 */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Accessibility", meta = (WorldContext = "WorldContextObject"))
	static FLinearColor ApplyColorblindForContext(const UObject* WorldContextObject, const FLinearColor& InColor);

	/**
	 * Multiply a base value (font size, widget padding, slot dimension, ...) by the current UI scale for
	 * the given world context. Falls back to BaseValue * 1.0 when no subsystem is reachable.
	 */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Accessibility", meta = (WorldContext = "WorldContextObject"))
	static float ScaledUIValue(const UObject* WorldContextObject, float BaseValue);

	/**
	 * Map the current subtitle-size preset for the given world context onto a concrete font-point size by
	 * interpolating between BaseSizePoints (Small) and BaseSizePoints * MaxScale (ExtraLarge). Lets a
	 * subtitle widget turn the abstract preset into pixels without hardcoding a switch in every project.
	 */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Accessibility", meta = (WorldContext = "WorldContextObject"))
	static float SubtitleFontPointSize(const UObject* WorldContextObject, float BaseSizePoints = 18.0f, float MaxScale = 1.75f);

	/** Pure mapping from a subtitle-size preset to a 0..1 step (Small=0, ExtraLarge=1). No world context needed. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Accessibility")
	static float SubtitleSizeAlpha(ESeam_SubtitleSize Size);
};
