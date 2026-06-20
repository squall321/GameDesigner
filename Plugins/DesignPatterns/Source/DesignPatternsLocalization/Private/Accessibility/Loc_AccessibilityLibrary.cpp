// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Accessibility/Loc_AccessibilityLibrary.h"

#include "Accessibility/Loc_AccessibilitySubsystem.h"

namespace Loc_AccessibilityLibraryPrivate
{
	/**
	 * Daltonization-style correction matrices (operating on linear RGB). For each dichromacy we shift the
	 * energy the affected cones cannot distinguish into channels the viewer CAN see, increasing hue
	 * separation. These are widely-used perceptual-aid approximations, not a clinical simulation. Rows are
	 * applied as: out = M * in. Identity for None.
	 */
	static FMatrix44f GetColorblindCorrectionMatrix(ESeam_ColorblindMode Mode)
	{
		switch (Mode)
		{
		case ESeam_ColorblindMode::Protanopia:
			// Redirect lost long-wavelength (red) signal toward green/blue.
			return FMatrix44f(
				FPlane4f(0.56667f, 0.43333f, 0.00000f, 0.f),
				FPlane4f(0.55833f, 0.44167f, 0.00000f, 0.f),
				FPlane4f(0.00000f, 0.24167f, 0.75833f, 0.f),
				FPlane4f(0.f, 0.f, 0.f, 1.f));

		case ESeam_ColorblindMode::Deuteranopia:
			// Redirect lost medium-wavelength (green) signal.
			return FMatrix44f(
				FPlane4f(0.62500f, 0.37500f, 0.00000f, 0.f),
				FPlane4f(0.70000f, 0.30000f, 0.00000f, 0.f),
				FPlane4f(0.00000f, 0.30000f, 0.70000f, 0.f),
				FPlane4f(0.f, 0.f, 0.f, 1.f));

		case ESeam_ColorblindMode::Tritanopia:
			// Redirect lost short-wavelength (blue) signal.
			return FMatrix44f(
				FPlane4f(0.95000f, 0.05000f, 0.00000f, 0.f),
				FPlane4f(0.00000f, 0.43333f, 0.56667f, 0.f),
				FPlane4f(0.00000f, 0.47500f, 0.52500f, 0.f),
				FPlane4f(0.f, 0.f, 0.f, 1.f));

		case ESeam_ColorblindMode::None:
		default:
			return FMatrix44f::Identity;
		}
	}
}

FSeam_AccessibilityOptions ULoc_AccessibilityLibrary::GetAccessibilityOptions(const UObject* WorldContextObject)
{
	if (const ULoc_AccessibilitySubsystem* Subsystem = ULoc_AccessibilitySubsystem::Get(WorldContextObject))
	{
		return Subsystem->GetOptions();
	}
	// Documented fallback: no subsystem reachable -> ship the seam struct's compiled-in defaults.
	return FSeam_AccessibilityOptions();
}

FLinearColor ULoc_AccessibilityLibrary::ApplyColorblindToColor(const FLinearColor& InColor, ESeam_ColorblindMode Mode)
{
	if (Mode == ESeam_ColorblindMode::None)
	{
		return InColor;
	}

	const FMatrix44f M = Loc_AccessibilityLibraryPrivate::GetColorblindCorrectionMatrix(Mode);

	const float R = M.M[0][0] * InColor.R + M.M[0][1] * InColor.G + M.M[0][2] * InColor.B;
	const float G = M.M[1][0] * InColor.R + M.M[1][1] * InColor.G + M.M[1][2] * InColor.B;
	const float B = M.M[2][0] * InColor.R + M.M[2][1] * InColor.G + M.M[2][2] * InColor.B;

	return FLinearColor(
		FMath::Clamp(R, 0.0f, 1.0f),
		FMath::Clamp(G, 0.0f, 1.0f),
		FMath::Clamp(B, 0.0f, 1.0f),
		InColor.A);
}

FLinearColor ULoc_AccessibilityLibrary::ApplyColorblindForContext(const UObject* WorldContextObject, const FLinearColor& InColor)
{
	const FSeam_AccessibilityOptions Options = GetAccessibilityOptions(WorldContextObject);
	return ApplyColorblindToColor(InColor, Options.ColorblindMode);
}

float ULoc_AccessibilityLibrary::ScaledUIValue(const UObject* WorldContextObject, float BaseValue)
{
	const FSeam_AccessibilityOptions Options = GetAccessibilityOptions(WorldContextObject);
	return BaseValue * Options.UIScale;
}

float ULoc_AccessibilityLibrary::SubtitleSizeAlpha(ESeam_SubtitleSize Size)
{
	switch (Size)
	{
	case ESeam_SubtitleSize::Small:      return 0.0f;
	case ESeam_SubtitleSize::Medium:     return 1.0f / 3.0f;
	case ESeam_SubtitleSize::Large:      return 2.0f / 3.0f;
	case ESeam_SubtitleSize::ExtraLarge: return 1.0f;
	default:                             return 1.0f / 3.0f;
	}
}

float ULoc_AccessibilityLibrary::SubtitleFontPointSize(const UObject* WorldContextObject, float BaseSizePoints, float MaxScale)
{
	const FSeam_AccessibilityOptions Options = GetAccessibilityOptions(WorldContextObject);
	const float Alpha = SubtitleSizeAlpha(Options.SubtitleSize);
	// Interpolate the multiplier from 1.0 (Small) up to MaxScale (ExtraLarge), then also honor global UIScale.
	const float SizeMultiplier = FMath::Lerp(1.0f, FMath::Max(MaxScale, 1.0f), Alpha);
	return BaseSizePoints * SizeMultiplier * Options.UIScale;
}
