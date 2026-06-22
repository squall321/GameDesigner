// Copyright DesignPatterns plugin. All Rights Reserved.

#include "VO/Audio_VOBankDataAsset.h"
#include "DesignPatternsAudioModule.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

const FAudio_VOEntry* UAudio_VOBankDataAsset::FindLine(const FGameplayTag& LineTag) const
{
	return Lines.Find(LineTag);
}

FName UAudio_VOBankDataAsset::GetDataAssetType_Implementation() const
{
	return FName(TEXT("Audio_VOBank"));
}

#if WITH_EDITOR
EDataValidationResult UAudio_VOBankDataAsset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	const FGameplayTag CategoryRoot = AudioNativeTags::Category;
	for (const TPair<FGameplayTag, FAudio_VOEntry>& Pair : Lines)
	{
		if (Pair.Value.Sound.IsNull())
		{
			Context.AddError(FText::FromString(
				FString::Printf(TEXT("VO line '%s' has no Sound assigned."), *Pair.Key.ToString())));
			Result = EDataValidationResult::Invalid;
		}
		if (Pair.Value.Category.IsValid() && CategoryRoot.IsValid()
			&& !Pair.Value.Category.MatchesTag(CategoryRoot))
		{
			Context.AddWarning(FText::FromString(
				FString::Printf(TEXT("VO line '%s' category '%s' is outside DP.Audio.Category."),
					*Pair.Key.ToString(), *Pair.Value.Category.ToString())));
		}
	}

	return Result;
}
#endif
