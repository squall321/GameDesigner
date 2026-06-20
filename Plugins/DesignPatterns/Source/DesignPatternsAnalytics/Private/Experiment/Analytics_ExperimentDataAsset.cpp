// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Experiment/Analytics_ExperimentDataAsset.h"
#include "Core/DPLog.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

float UAnalytics_ExperimentDataAsset::GetTotalAssignableWeight() const
{
	float Total = 0.0f;
	for (const FAnalytics_ExperimentVariant& Variant : Variants)
	{
		if (Variant.IsAssignable())
		{
			Total += Variant.RolloutWeight;
		}
	}
	return Total;
}

bool UAnalytics_ExperimentDataAsset::HasAssignableVariants() const
{
	for (const FAnalytics_ExperimentVariant& Variant : Variants)
	{
		if (Variant.IsAssignable())
		{
			return true;
		}
	}
	return false;
}

FGameplayTag UAnalytics_ExperimentDataAsset::SelectVariantFromNormalized(float NormalizedSelector) const
{
	const float TotalWeight = GetTotalAssignableWeight();
	if (TotalWeight <= 0.0f)
	{
		// No assignable variants — inert default. Documented fallback, not a magic value.
		return DefaultVariantTag;
	}

	// Defensive clamp: the caller is contractually supposed to pass [0,1), but a hash that maps
	// to exactly 1.0 (or any out-of-range float) must still land in a valid bucket rather than
	// fall off the end of the accumulation loop.
	const float ClampedSelector = FMath::Clamp(NormalizedSelector, 0.0f, FMath::Max(0.0f, 1.0f - SMALL_NUMBER));
	const float TargetWeight = ClampedSelector * TotalWeight;

	float Accumulated = 0.0f;
	FGameplayTag LastAssignable = DefaultVariantTag;
	for (const FAnalytics_ExperimentVariant& Variant : Variants)
	{
		if (!Variant.IsAssignable())
		{
			continue;
		}
		LastAssignable = Variant.VariantTag;
		Accumulated += Variant.RolloutWeight;
		if (TargetWeight < Accumulated)
		{
			return Variant.VariantTag;
		}
	}

	// Floating-point round-off can leave TargetWeight == TotalWeight; return the last assignable
	// bucket so the full [0,1) range is covered with no gap.
	return LastAssignable;
}

bool UAnalytics_ExperimentDataAsset::IsKnownVariant(FGameplayTag VariantTag) const
{
	if (!VariantTag.IsValid())
	{
		return false;
	}
	for (const FAnalytics_ExperimentVariant& Variant : Variants)
	{
		if (Variant.VariantTag == VariantTag)
		{
			return true;
		}
	}
	return false;
}

#if WITH_EDITOR
EDataValidationResult UAnalytics_ExperimentDataAsset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	TSet<FGameplayTag> Seen;
	for (int32 Index = 0; Index < Variants.Num(); ++Index)
	{
		const FAnalytics_ExperimentVariant& Variant = Variants[Index];

		if (!Variant.VariantTag.IsValid())
		{
			Context.AddError(FText::Format(
				NSLOCTEXT("DesignPatternsAnalytics", "ExperimentVariantNoTag",
					"Experiment variant at index {0} has an invalid (empty) VariantTag."),
				FText::AsNumber(Index)));
			Result = EDataValidationResult::Invalid;
		}
		else if (Seen.Contains(Variant.VariantTag))
		{
			Context.AddError(FText::Format(
				NSLOCTEXT("DesignPatternsAnalytics", "ExperimentVariantDuplicate",
					"Experiment variant tag '{0}' is duplicated; variant tags must be unique."),
				FText::FromName(Variant.VariantTag.GetTagName())));
			Result = EDataValidationResult::Invalid;
		}
		else
		{
			Seen.Add(Variant.VariantTag);
		}

		if (Variant.RolloutWeight < 0.0f)
		{
			Context.AddError(FText::Format(
				NSLOCTEXT("DesignPatternsAnalytics", "ExperimentVariantNegativeWeight",
					"Experiment variant '{0}' has a negative RolloutWeight."),
				FText::FromName(Variant.VariantTag.GetTagName())));
			Result = EDataValidationResult::Invalid;
		}
	}

	if (!DefaultVariantTag.IsValid())
	{
		Context.AddWarning(NSLOCTEXT("DesignPatternsAnalytics", "ExperimentNoDefault",
			"Experiment has no DefaultVariantTag; GetVariant will return an empty tag in the fallback path."));
	}
	else if (Variants.Num() > 0 && !IsKnownVariant(DefaultVariantTag))
	{
		// Not fatal: a default that is the 'control / no-treatment' bucket need not be a weighted
		// variant. Warn so the designer confirms it is intentional rather than a typo.
		Context.AddWarning(FText::Format(
			NSLOCTEXT("DesignPatternsAnalytics", "ExperimentDefaultNotAVariant",
				"DefaultVariantTag '{0}' is not one of the declared variants. This is allowed (control bucket) but often a mistake."),
			FText::FromName(DefaultVariantTag.GetTagName())));
	}

	return Result;
}
#endif // WITH_EDITOR
