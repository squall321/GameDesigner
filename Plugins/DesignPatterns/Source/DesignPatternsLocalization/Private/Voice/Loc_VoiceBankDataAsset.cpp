// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Voice/Loc_VoiceBankDataAsset.h"
#include "Sound/SoundBase.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

const FLoc_VoiceLineRow* ULoc_VoiceBankDataAsset::FindRow(FGameplayTag LineId) const
{
	return Lines.Find(LineId);
}

TArray<FSoftObjectPath> ULoc_VoiceBankDataAsset::GetLineSoundPaths() const
{
	TArray<FSoftObjectPath> Paths;
	Paths.Reserve(Lines.Num());
	for (const TPair<FGameplayTag, FLoc_VoiceLineRow>& Pair : Lines)
	{
		if (!Pair.Value.SoundAsset.IsNull())
		{
			Paths.Add(Pair.Value.SoundAsset.ToSoftObjectPath());
		}
	}
	return Paths;
}

FName ULoc_VoiceBankDataAsset::GetDataAssetType_Implementation() const
{
	// Stable bucket shared by every voice bank regardless of culture/subclass.
	return FName(TEXT("Loc_VoiceBank"));
}

#if WITH_EDITOR
EDataValidationResult ULoc_VoiceBankDataAsset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (Culture.IsEmpty())
	{
		Context.AddError(FText::FromString(TEXT("Loc_VoiceBank: Culture is empty; this bank can never be selected for any active culture.")));
		Result = EDataValidationResult::Invalid;
	}

	for (const TPair<FGameplayTag, FLoc_VoiceLineRow>& Pair : Lines)
	{
		if (!Pair.Key.IsValid())
		{
			Context.AddError(FText::FromString(TEXT("Loc_VoiceBank: a line entry has an invalid (empty) line-id tag.")));
			Result = EDataValidationResult::Invalid;
			continue;
		}

		// A row with neither audio nor a subtitle key is meaningless — warn (not fatal: a caption-only row
		// is a legitimate authoring choice, but an empty row is almost certainly a mistake).
		if (Pair.Value.SoundAsset.IsNull() && !Pair.Value.SubtitleKey.IsValid())
		{
			Context.AddWarning(FText::FromString(
				FString::Printf(TEXT("Loc_VoiceBank: line '%s' has no SoundAsset and no SubtitleKey (empty row)."),
					*Pair.Key.ToString())));
		}
	}

	return Result;
}
#endif
