// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Data/Audio_SoundBankDataAsset.h"
#include "DesignPatternsAudioModule.h"
#include "Core/DPLog.h"
#include "GameplayTagContainer.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "Audio_SoundBank"

const FAudio_SoundEntry* UAudio_SoundBankDataAsset::FindEntry(const FGameplayTag& SoundTag) const
{
	if (!SoundTag.IsValid())
	{
		return nullptr;
	}
	return Entries.Find(SoundTag);
}

FName UAudio_SoundBankDataAsset::GetDataAssetType_Implementation() const
{
	// All sound banks share one asset-manager bucket so a project can scan/cook them as a family.
	return FName(TEXT("Audio_SoundBank"));
}

#if WITH_EDITOR
EDataValidationResult UAudio_SoundBankDataAsset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	for (const TPair<FGameplayTag, FAudio_SoundEntry>& Pair : Entries)
	{
		const FGameplayTag& Key = Pair.Key;
		const FAudio_SoundEntry& Entry = Pair.Value;

		if (!Key.IsValid())
		{
			Context.AddError(LOCTEXT("InvalidKey", "Sound bank has an entry with an invalid (empty) sound tag key."));
			Result = EDataValidationResult::Invalid;
		}

		if (Entry.Sound.IsNull())
		{
			Context.AddError(FText::Format(
				LOCTEXT("NullSound", "Sound bank entry '{0}' has no Sound asset assigned."),
				FText::FromString(Key.ToString())));
			Result = EDataValidationResult::Invalid;
		}

		// Category is optional (invalid == uncategorized), but if set it must live under the audio
		// category root so concurrency/volume/ducking can address it via tag hierarchy.
		if (Entry.Category.IsValid() && !Entry.Category.MatchesTag(AudioNativeTags::Category))
		{
			Context.AddWarning(FText::Format(
				LOCTEXT("CategoryOutsideRoot", "Sound bank entry '{0}' has Category '{1}' which is not under DP.Audio.Category."),
				FText::FromString(Key.ToString()),
				FText::FromString(Entry.Category.ToString())));
		}
	}

	return Result;
}
#endif

#undef LOCTEXT_NAMESPACE
