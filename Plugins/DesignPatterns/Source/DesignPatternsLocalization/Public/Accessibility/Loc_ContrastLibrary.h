// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Loc/Seam_AccessibilityTypes.h"
#include "Loc_ContrastLibrary.generated.h"

class ULoc_ColorblindLUTDataAsset;

/**
 * Stateless high-contrast / colorblind helpers that EXTEND (never modify) ULoc_AccessibilityLibrary. These
 * provide sharper, opt-in corrections on top of the canonical inline ApplyColorblindToColor (which stays
 * authoritative). World-context overloads read the live accessibility mode via
 * ULoc_AccessibilityLibrary::GetAccessibilityOptions so callers need no subsystem reference.
 *
 * All math is in linear space and preserves alpha. No magic palette is hardcoded — high-contrast snapping
 * targets pure luminance extremes derived from the input, and the colorblind LUT path is fully data-driven.
 */
UCLASS()
class DESIGNPATTERNSLOCALIZATION_API ULoc_ContrastLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Snap In toward a maximum-contrast version of itself: pushes its luminance toward black or white
	 * (whichever is farther) while preserving hue, so foreground text "pops" in a high-contrast mode. Only
	 * applies when the world context's accessibility options request high UI scale-independent contrast; a
	 * project drives this by calling it where a high-contrast toggle is honored. The standalone overload
	 * always applies.
	 */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Accessibility", meta = (WorldContext = "WorldContextObject"))
	static FLinearColor ApplyHighContrast(const UObject* WorldContextObject, const FLinearColor& In);

	/** Standalone high-contrast snap (no world context); always applies. Strength 0..1 controls how far. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Accessibility")
	static FLinearColor HighContrastColor(const FLinearColor& In, float Strength = 1.0f);

	/**
	 * Adjust Fg's luminance just enough to meet a WCAG contrast ratio against Bg. Moves Fg lighter or darker
	 * (whichever direction reaches the ratio with the smaller change), preserving hue. If the ratio is
	 * already met, returns Fg unchanged. MinRatio is clamped to the valid WCAG range [1, 21].
	 */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Accessibility")
	static FLinearColor EnsureContrastRatio(const FLinearColor& Fg, const FLinearColor& Bg, float MinRatio = 4.5f);

	/**
	 * Apply a data-asset daltonization LUT for Mode. With a null Lut or ESeam_ColorblindMode::None this is
	 * the identity. Sharper / tunable alternative to the canonical inline ApplyColorblindToColor.
	 */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Accessibility")
	static FLinearColor ApplyColorblindLUT(const FLinearColor& In, const ULoc_ColorblindLUTDataAsset* Lut, ESeam_ColorblindMode Mode);

	/**
	 * Compute the WCAG contrast ratio (1..21) between two linear colors. Useful for QA/tooling that wants to
	 * verify a palette meets accessibility thresholds. Wraps the relative-luminance formula.
	 */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Accessibility")
	static float ContrastRatio(const FLinearColor& A, const FLinearColor& B);

private:
	/** WCAG relative luminance of a linear color (already linear; no sRGB decode needed). */
	static float RelativeLuminance(const FLinearColor& C);
};
