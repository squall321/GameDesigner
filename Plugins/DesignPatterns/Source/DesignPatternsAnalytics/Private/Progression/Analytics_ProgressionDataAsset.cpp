// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Progression/Analytics_ProgressionDataAsset.h"
#include "Core/DPLog.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

int32 UAnalytics_ProgressionDataAsset::IndexOfStep(FGameplayTag StepTag) const
{
	if (!StepTag.IsValid())
	{
		return INDEX_NONE;
	}
	for (int32 Index = 0; Index < Steps.Num(); ++Index)
	{
		if (Steps[Index].StepTag == StepTag)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

bool UAnalytics_ProgressionDataAsset::ContainsStep(FGameplayTag StepTag) const
{
	return IndexOfStep(StepTag) != INDEX_NONE;
}

bool UAnalytics_ProgressionDataAsset::IsMilestoneStep(FGameplayTag StepTag) const
{
	const int32 Index = IndexOfStep(StepTag);
	return Steps.IsValidIndex(Index) && Steps[Index].bIsMilestone;
}

#if WITH_EDITOR
EDataValidationResult UAnalytics_ProgressionDataAsset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	TSet<FGameplayTag> Seen;
	for (int32 Index = 0; Index < Steps.Num(); ++Index)
	{
		const FAnalytics_FunnelStep& Step = Steps[Index];
		if (!Step.StepTag.IsValid())
		{
			Context.AddError(FText::Format(
				NSLOCTEXT("DesignPatternsAnalytics", "FunnelStepNoTag",
					"Funnel step at index {0} has an invalid (empty) StepTag."),
				FText::AsNumber(Index)));
			Result = EDataValidationResult::Invalid;
		}
		else if (Seen.Contains(Step.StepTag))
		{
			Context.AddError(FText::Format(
				NSLOCTEXT("DesignPatternsAnalytics", "FunnelStepDuplicate",
					"Funnel step tag '{0}' is duplicated; step tags must be unique within a funnel."),
				FText::FromName(Step.StepTag.GetTagName())));
			Result = EDataValidationResult::Invalid;
		}
		else
		{
			Seen.Add(Step.StepTag);
		}
	}

	return Result;
}
#endif // WITH_EDITOR
