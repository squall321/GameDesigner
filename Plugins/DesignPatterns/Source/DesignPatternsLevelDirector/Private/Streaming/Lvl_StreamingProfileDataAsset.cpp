// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Streaming/Lvl_StreamingProfileDataAsset.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "Lvl_StreamingProfileDataAsset"

namespace
{
	/** Defensive minimum cadence so a misconfigured 0 cannot run the watcher every tick. */
	constexpr float GMinEvaluationInterval = 0.05f;
}

ULvl_StreamingProfileDataAsset::ULvl_StreamingProfileDataAsset()
{
}

float ULvl_StreamingProfileDataAsset::GetEffectiveBudgetMB() const
{
	return FMath::Max(1.f, MemoryBudgetMB);
}

float ULvl_StreamingProfileDataAsset::GetEffectiveEvaluationInterval() const
{
	return FMath::Max(GMinEvaluationInterval, EvaluationIntervalSeconds);
}

float ULvl_StreamingProfileDataAsset::GetCategoryPriority(FGameplayTag Category) const
{
	if (Category.IsValid())
	{
		if (const float* Found = CategoryPriority.Find(Category))
		{
			return FMath::Max(0.f, *Found);
		}
	}
	return FMath::Max(0.f, DefaultCategoryPriority);
}

float ULvl_StreamingProfileDataAsset::GetEstimatedMBPerResidentLevel() const
{
	return FMath::Max(0.f, EstimatedMBPerResidentLevel);
}

#if WITH_EDITOR
EDataValidationResult ULvl_StreamingProfileDataAsset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (MemoryBudgetMB <= 0.f)
	{
		Context.AddError(LOCTEXT("BadBudget", "MemoryBudgetMB must be > 0."));
		Result = EDataValidationResult::Invalid;
	}
	if (PressureSaturationOvershoot <= 0.f)
	{
		Context.AddError(LOCTEXT("BadOvershoot", "PressureSaturationOvershoot must be > 0."));
		Result = EDataValidationResult::Invalid;
	}
	for (const TPair<FGameplayTag, float>& Pair : CategoryPriority)
	{
		if (Pair.Value < 0.f)
		{
			Context.AddWarning(LOCTEXT("NegPriority", "A CategoryPriority weight is negative; it will be clamped to 0."));
		}
	}
	return Result;
}
#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE
