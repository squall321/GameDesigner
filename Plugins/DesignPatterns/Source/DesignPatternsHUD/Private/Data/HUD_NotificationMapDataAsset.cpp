// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Data/HUD_NotificationMapDataAsset.h"
#include "Core/DPLog.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

void UHUD_NotificationMapDataAsset::GetSubscribedChannels(TArray<FGameplayTag>& OutChannels) const
{
	OutChannels.Reset();
	for (const FHUD_NotificationMapEntry& Entry : Entries)
	{
		if (Entry.BusChannel.IsValid())
		{
			OutChannels.AddUnique(Entry.BusChannel);
		}
	}
}

const FHUD_NotificationMapEntry* UHUD_NotificationMapDataAsset::FindBestRule(const FGameplayTag& BroadcastChannel) const
{
	if (!BroadcastChannel.IsValid())
	{
		return nullptr;
	}

	const FHUD_NotificationMapEntry* Best = nullptr;
	int32 BestDepth = -1;

	for (const FHUD_NotificationMapEntry& Entry : Entries)
	{
		if (!Entry.BusChannel.IsValid())
		{
			continue;
		}

		// Hierarchy-aware: the broadcast channel must be the rule's channel or a child of it.
		if (BroadcastChannel.MatchesTag(Entry.BusChannel))
		{
			// Depth = number of tag segments; deeper (more specific) rule wins.
			const int32 Depth = Entry.BusChannel.ToString().Len();
			if (Depth > BestDepth)
			{
				Best = &Entry;
				BestDepth = Depth;
			}
		}
	}

	return Best;
}

FName UHUD_NotificationMapDataAsset::GetDataAssetType_Implementation() const
{
	return FName(TEXT("HUD_NotificationMap"));
}

#if WITH_EDITOR
EDataValidationResult UHUD_NotificationMapDataAsset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		const FHUD_NotificationMapEntry& Entry = Entries[Index];

		if (!Entry.BusChannel.IsValid())
		{
			Context.AddError(FText::FromString(
				FString::Printf(TEXT("Notification map entry [%d] has an invalid BusChannel."), Index)));
			Result = EDataValidationResult::Invalid;
		}

		if (!Entry.Template.HasContent())
		{
			Context.AddError(FText::FromString(
				FString::Printf(TEXT("Notification map entry [%d] (channel '%s') has an empty Title and Body."),
					Index, *Entry.BusChannel.ToString())));
			Result = EDataValidationResult::Invalid;
		}
	}

	return Result;
}
#endif // WITH_EDITOR
