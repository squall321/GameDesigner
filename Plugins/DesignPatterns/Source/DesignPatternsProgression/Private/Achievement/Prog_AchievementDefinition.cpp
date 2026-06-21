// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Achievement/Prog_AchievementDefinition.h"
#include "Achievement/Prog_Condition.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "Prog_AchievementDefinition"

UProg_AchievementDefinition::UProg_AchievementDefinition()
{
	// Defaults are declared inline on the members.
}

FName UProg_AchievementDefinition::GetDataAssetType_Implementation() const
{
	// Collapse every achievement definition into a single asset-manager type so the achievement
	// catalog can enumerate / preload them as one PrimaryAssetType.
	return FName(TEXT("Prog_Achievement"));
}

#if WITH_EDITOR
EDataValidationResult UProg_AchievementDefinition::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (Conditions.Num() == 0)
	{
		Context.AddWarning(LOCTEXT("NoConditions",
			"Achievement has no Conditions; it will never unlock automatically and must be unlocked explicitly."));
	}
	else
	{
		for (int32 Index = 0; Index < Conditions.Num(); ++Index)
		{
			if (Conditions[Index] == nullptr)
			{
				Context.AddError(FText::Format(LOCTEXT("NullCondition", "Condition at index {0} is null."),
					FText::AsNumber(Index)));
				Result = EDataValidationResult::Invalid;
			}
		}
	}

	if (RewardCurrency.IsValid() && RewardAmount <= 0)
	{
		Context.AddWarning(LOCTEXT("RewardZero",
			"RewardCurrency is set but RewardAmount is 0; no currency will be granted on unlock."));
	}
	if (!RewardCurrency.IsValid() && RewardAmount > 0)
	{
		Context.AddWarning(LOCTEXT("RewardNoCurrency",
			"RewardAmount is non-zero but RewardCurrency is unset; no currency will be granted."));
	}

	return Result;
}
#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE
