// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Accessibility/Loc_ContrastLibrary.h"
#include "Accessibility/Loc_ColorblindLUTDataAsset.h"
#include "Accessibility/Loc_AccessibilityLibrary.h"

float ULoc_ContrastLibrary::RelativeLuminance(const FLinearColor& C)
{
	// Rec.709 luma weights on already-linear channels. Clamp to [0,1] defensively (HDR colors can exceed 1).
	const float R = FMath::Clamp(C.R, 0.f, 1.f);
	const float G = FMath::Clamp(C.G, 0.f, 1.f);
	const float B = FMath::Clamp(C.B, 0.f, 1.f);
	return 0.2126f * R + 0.7152f * G + 0.0722f * B;
}

float ULoc_ContrastLibrary::ContrastRatio(const FLinearColor& A, const FLinearColor& B)
{
	const float La = RelativeLuminance(A);
	const float Lb = RelativeLuminance(B);
	const float Lighter = FMath::Max(La, Lb);
	const float Darker = FMath::Min(La, Lb);
	// WCAG: (L1 + 0.05) / (L2 + 0.05). Range [1, 21].
	return (Lighter + 0.05f) / (Darker + 0.05f);
}

FLinearColor ULoc_ContrastLibrary::HighContrastColor(const FLinearColor& In, float Strength)
{
	const float S = FMath::Clamp(Strength, 0.f, 1.f);

	// Decide the target extreme: push toward whichever of black/white is FARTHER from the input luminance,
	// so a mid-gray jumps to the more separated end. Preserve hue by scaling the original chroma toward the
	// extreme rather than zeroing it.
	const float Lum = RelativeLuminance(In);
	const bool bTowardWhite = Lum < 0.5f;

	const FLinearColor Target = bTowardWhite ? FLinearColor::White : FLinearColor::Black;

	FLinearColor Out;
	Out.R = FMath::Lerp(In.R, Target.R, S);
	Out.G = FMath::Lerp(In.G, Target.G, S);
	Out.B = FMath::Lerp(In.B, Target.B, S);
	Out.A = In.A;
	return Out;
}

FLinearColor ULoc_ContrastLibrary::ApplyHighContrast(const UObject* WorldContextObject, const FLinearColor& In)
{
	// Read the live options to decide whether to apply. We use the colorblind-independent path; projects
	// that gate high-contrast on a specific option can call HighContrastColor directly. Here we apply at
	// full strength whenever any accessibility customization is active (UIScale != 1 or a colorblind mode),
	// a conservative heuristic that never over-applies on a fully-default config.
	const FSeam_AccessibilityOptions Options = ULoc_AccessibilityLibrary::GetAccessibilityOptions(WorldContextObject);
	const bool bAnyAccessibilityActive =
		Options.ColorblindMode != ESeam_ColorblindMode::None ||
		!FMath::IsNearlyEqual(Options.UIScale, 1.0f);

	if (!bAnyAccessibilityActive)
	{
		return In;
	}
	return HighContrastColor(In, 1.0f);
}

FLinearColor ULoc_ContrastLibrary::EnsureContrastRatio(const FLinearColor& Fg, const FLinearColor& Bg, float MinRatio)
{
	const float Target = FMath::Clamp(MinRatio, 1.0f, 21.0f);

	if (ContrastRatio(Fg, Bg) >= Target)
	{
		return Fg; // already meets the ratio.
	}

	// Try darkening and lightening toward the extremes; pick the smaller change that reaches the ratio.
	auto TryBlend = [&Fg, &Bg, Target](const FLinearColor& Extreme) -> TOptional<FLinearColor>
	{
		// Binary search the blend alpha that first reaches the target ratio.
		float Lo = 0.f, Hi = 1.f;
		FLinearColor Best;
		bool bFound = false;
		for (int32 Iter = 0; Iter < 12; ++Iter)
		{
			const float Mid = 0.5f * (Lo + Hi);
			FLinearColor Candidate;
			Candidate.R = FMath::Lerp(Fg.R, Extreme.R, Mid);
			Candidate.G = FMath::Lerp(Fg.G, Extreme.G, Mid);
			Candidate.B = FMath::Lerp(Fg.B, Extreme.B, Mid);
			Candidate.A = Fg.A;

			if (ContrastRatio(Candidate, Bg) >= Target)
			{
				Best = Candidate;
				bFound = true;
				Hi = Mid; // try a smaller change.
			}
			else
			{
				Lo = Mid;
			}
		}
		return bFound ? TOptional<FLinearColor>(Best) : TOptional<FLinearColor>();
	};

	const TOptional<FLinearColor> Darker = TryBlend(FLinearColor::Black);
	const TOptional<FLinearColor> Lighter = TryBlend(FLinearColor::White);

	if (Darker.IsSet() && Lighter.IsSet())
	{
		// Both reach it; pick the one closer (smaller luminance delta) to the original for a subtler change.
		const float DDelta = FMath::Abs(RelativeLuminance(Darker.GetValue()) - RelativeLuminance(Fg));
		const float LDelta = FMath::Abs(RelativeLuminance(Lighter.GetValue()) - RelativeLuminance(Fg));
		return (DDelta <= LDelta) ? Darker.GetValue() : Lighter.GetValue();
	}
	if (Darker.IsSet())
	{
		return Darker.GetValue();
	}
	if (Lighter.IsSet())
	{
		return Lighter.GetValue();
	}

	// Could not reach the ratio against this background (e.g. mid-gray bg); return the higher-contrast
	// extreme as a best effort.
	return (ContrastRatio(FLinearColor::White, Bg) >= ContrastRatio(FLinearColor::Black, Bg))
		? FLinearColor(FLinearColor::White.R, FLinearColor::White.G, FLinearColor::White.B, Fg.A)
		: FLinearColor(FLinearColor::Black.R, FLinearColor::Black.G, FLinearColor::Black.B, Fg.A);
}

FLinearColor ULoc_ContrastLibrary::ApplyColorblindLUT(const FLinearColor& In, const ULoc_ColorblindLUTDataAsset* Lut, ESeam_ColorblindMode Mode)
{
	if (!Lut || Mode == ESeam_ColorblindMode::None)
	{
		return In; // identity.
	}
	return Lut->Sample(Mode, In);
}
