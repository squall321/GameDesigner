// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Mix/Audio_MixProfileDataAsset.h"
#include "DesignPatternsAudioModule.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "Audio_MixProfile"

FName UAudio_MixProfileDataAsset::GetDataAssetType_Implementation() const
{
	return FName(TEXT("Audio_MixProfile"));
}

#if WITH_EDITOR
EDataValidationResult UAudio_MixProfileDataAsset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	for (int32 Index = 0; Index < SubmixOverrides.Num(); ++Index)
	{
		if (SubmixOverrides[Index].Submix.IsNull())
		{
			Context.AddError(FText::Format(
				LOCTEXT("NullSubmix", "Mix profile submix override [{0}] has no Submix assigned."),
				FText::AsNumber(Index)));
			Result = EDataValidationResult::Invalid;
		}
	}

	for (int32 Index = 0; Index < DuckRules.Num(); ++Index)
	{
		const FAudio_DuckRule& Rule = DuckRules[Index];
		if (!Rule.TargetCategory.IsValid())
		{
			Context.AddError(FText::Format(
				LOCTEXT("NullDuckCategory", "Mix profile duck rule [{0}] has no TargetCategory."),
				FText::AsNumber(Index)));
			Result = EDataValidationResult::Invalid;
		}
		else if (!Rule.TargetCategory.MatchesTag(AudioNativeTags::Category))
		{
			Context.AddWarning(FText::Format(
				LOCTEXT("DuckCategoryOutsideRoot", "Mix profile duck rule [{0}] targets '{1}', not under DP.Audio.Category."),
				FText::AsNumber(Index),
				FText::FromString(Rule.TargetCategory.ToString())));
		}
	}

	return Result;
}
#endif

#undef LOCTEXT_NAMESPACE
