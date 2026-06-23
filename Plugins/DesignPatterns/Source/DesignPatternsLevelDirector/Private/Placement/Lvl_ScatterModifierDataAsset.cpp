// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Placement/Lvl_ScatterModifierDataAsset.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "Lvl_ScatterModifierDataAsset"

ULvl_ScatterModifierDataAsset::ULvl_ScatterModifierDataAsset()
{
}

float ULvl_ScatterModifierDataAsset::SampleDistanceFieldWeight(float NormalizedDistance01) const
{
	const FRichCurve* Curve = DistanceFieldFalloff.GetRichCurveConst();
	float Weight = 1.f;
	if (Curve && Curve->GetNumKeys() > 0)
	{
		Weight = Curve->Eval(FMath::Clamp(NormalizedDistance01, 0.f, 1.f), 1.f);
	}
	// Apply the global density multiplier and clamp to a valid probability.
	return FMath::Clamp(Weight * FMath::Clamp(DensityThreshold, 0.f, 1.f), 0.f, 1.f);
}

bool ULvl_ScatterModifierDataAsset::HasUsableClass() const
{
	for (const FGameplayTag& Tag : ScatterClassTags)
	{
		if (Tag.IsValid())
		{
			return true;
		}
	}
	return false;
}

#if WITH_EDITOR
EDataValidationResult ULvl_ScatterModifierDataAsset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (PoissonMinRadius <= 0.f)
	{
		Context.AddError(LOCTEXT("BadRadius", "PoissonMinRadius must be > 0."));
		Result = EDataValidationResult::Invalid;
	}
	if (SampleAreaExtent.X <= 0.f || SampleAreaExtent.Y <= 0.f)
	{
		Context.AddError(LOCTEXT("BadExtent", "SampleAreaExtent must be > 0 in each axis."));
		Result = EDataValidationResult::Invalid;
	}
	if (!HasUsableClass())
	{
		Context.AddWarning(LOCTEXT("NoClass", "No usable ScatterClassTags; the advanced placer will place nothing."));
	}
	return Result;
}
#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE
