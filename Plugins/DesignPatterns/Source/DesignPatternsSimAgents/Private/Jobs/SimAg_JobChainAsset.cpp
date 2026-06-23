// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Jobs/SimAg_JobChainAsset.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "SimAg_JobChainAsset"

USimAg_JobChainAsset::USimAg_JobChainAsset()
{
}

FSimAg_JobStep USimAg_JobChainAsset::GetNextEligibleStep(const FGameplayTagContainer& CompletedKinds) const
{
	for (const FSimAg_JobStep& Step : Steps)
	{
		if (!Step.JobKind.IsValid())
		{
			continue;
		}
		// Skip steps already done; pick the first whose prerequisites are satisfied.
		if (CompletedKinds.HasTag(Step.JobKind))
		{
			continue;
		}
		if (Step.IsEligible(CompletedKinds))
		{
			return Step;
		}
	}
	return FSimAg_JobStep();
}

bool USimAg_JobChainAsset::IsChainComplete(const FGameplayTagContainer& CompletedKinds) const
{
	for (const FSimAg_JobStep& Step : Steps)
	{
		if (Step.JobKind.IsValid() && !CompletedKinds.HasTag(Step.JobKind))
		{
			return false;
		}
	}
	return true;
}

FName USimAg_JobChainAsset::GetDataAssetType_Implementation() const
{
	// One shared bucket so all job chains group together in the asset manager regardless of subclass.
	return FName("SimAg_JobChain");
}

#if WITH_EDITOR
EDataValidationResult USimAg_JobChainAsset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	// Collect the kinds the chain provides so we can flag a prerequisite no step satisfies.
	FGameplayTagContainer ProvidedKinds;
	for (const FSimAg_JobStep& Step : Steps)
	{
		if (Step.JobKind.IsValid())
		{
			ProvidedKinds.AddTag(Step.JobKind);
		}
	}

	for (int32 Index = 0; Index < Steps.Num(); ++Index)
	{
		const FSimAg_JobStep& Step = Steps[Index];
		if (!Step.JobKind.IsValid())
		{
			Context.AddWarning(FText::Format(
				LOCTEXT("NoKind", "Job chain step {0} has no JobKind tag."), FText::AsNumber(Index)));
		}
		for (const FGameplayTag& Prereq : Step.Prerequisites)
		{
			if (!ProvidedKinds.HasTag(Prereq))
			{
				Context.AddWarning(FText::Format(
					LOCTEXT("DanglingPrereq", "Job chain step {0} requires '{1}', which no step in this chain provides."),
					FText::AsNumber(Index), FText::FromString(Prereq.ToString())));
			}
		}
	}

	return Result;
}
#endif

#undef LOCTEXT_NAMESPACE
