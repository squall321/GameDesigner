// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Vfx/WS_VfxBankDataAsset.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "WS_VfxBankDataAsset"

const FWS_VfxEntry* UWS_VfxBankDataAsset::FindEntry(const FGameplayTag& VfxTag) const
{
	if (!VfxTag.IsValid())
	{
		return nullptr;
	}
	return Entries.FindByPredicate([&VfxTag](const FWS_VfxEntry& Entry)
	{
		return Entry.VfxTag == VfxTag;
	});
}

#if WITH_EDITOR
EDataValidationResult UWS_VfxBankDataAsset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	TSet<FGameplayTag> SeenTags;
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		const FWS_VfxEntry& Entry = Entries[Index];

		if (!Entry.VfxTag.IsValid())
		{
			Context.AddError(FText::Format(
				LOCTEXT("WS_VfxInvalidTag", "VFX bank entry {0} has an invalid VfxTag."),
				FText::AsNumber(Index)));
			Result = EDataValidationResult::Invalid;
		}
		else if (SeenTags.Contains(Entry.VfxTag))
		{
			Context.AddWarning(FText::Format(
				LOCTEXT("WS_VfxDupTag", "VFX bank has a duplicate VfxTag '{0}'; only the first is used."),
				FText::FromString(Entry.VfxTag.ToString())));
		}
		else
		{
			SeenTags.Add(Entry.VfxTag);
		}

		if (Entry.System.IsNull())
		{
			Context.AddError(FText::Format(
				LOCTEXT("WS_VfxNullSystem", "VFX bank entry '{0}' has no particle system assigned."),
				FText::FromString(Entry.VfxTag.ToString())));
			Result = EDataValidationResult::Invalid;
		}
	}

	return Result;
}
#endif

#undef LOCTEXT_NAMESPACE
