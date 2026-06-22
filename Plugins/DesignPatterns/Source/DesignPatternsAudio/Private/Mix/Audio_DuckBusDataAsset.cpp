// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Mix/Audio_DuckBusDataAsset.h"
#include "DesignPatternsAudioModule.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

FName UAudio_DuckBusDataAsset::GetDataAssetType_Implementation() const
{
	return FName(TEXT("Audio_DuckBus"));
}

#if WITH_EDITOR
EDataValidationResult UAudio_DuckBusDataAsset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (Duckees.Num() == 0)
	{
		Context.AddWarning(FText::FromString(TEXT("Duck bus has no duckee categories; it will have no audible effect.")));
	}

	const FGameplayTag CategoryRoot = AudioNativeTags::Category;
	for (int32 Index = 0; Index < Duckees.Num(); ++Index)
	{
		const FAudio_DuckRule& Rule = Duckees[Index];
		if (!Rule.TargetCategory.IsValid())
		{
			Context.AddError(FText::FromString(
				FString::Printf(TEXT("Duckee [%d] has no TargetCategory."), Index)));
			Result = EDataValidationResult::Invalid;
		}
		else if (CategoryRoot.IsValid() && !Rule.TargetCategory.MatchesTag(CategoryRoot))
		{
			Context.AddWarning(FText::FromString(
				FString::Printf(TEXT("Duckee [%d] category '%s' is outside the DP.Audio.Category root."),
					Index, *Rule.TargetCategory.ToString())));
		}
	}

	return Result;
}
#endif
