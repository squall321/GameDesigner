// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Event/Analytics_EventMapDataAsset.h"
#include "Core/DPLog.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

const FAnalytics_EventMapEntry* UAnalytics_EventMapDataAsset::ResolveEntryForChannel(const FGameplayTag& Channel) const
{
	if (!Channel.IsValid())
	{
		return nullptr;
	}

	const FAnalytics_EventMapEntry* Best = nullptr;
	int32 BestDepth = -1;

	for (const FAnalytics_EventMapEntry& Entry : Entries)
	{
		if (!Entry.SourceBusChannel.IsValid() || !Entry.AnalyticsEvent.IsValid())
		{
			continue;
		}

		// The channel matches an entry when it equals the entry's source, or is a child of it.
		const bool bExact = (Channel == Entry.SourceBusChannel);
		const bool bChild = !bExact && Channel.MatchesTag(Entry.SourceBusChannel);
		if (!bExact && !bChild)
		{
			continue;
		}

		// Prefer the most specific source tag. Depth is the number of tag nodes; an exact match
		// is treated as one level deeper than the same tag matched as a parent so exact wins.
		const int32 Depth = Entry.SourceBusChannel.GetGameplayTagParents().Num() + (bExact ? 1 : 0);
		if (Depth > BestDepth)
		{
			BestDepth = Depth;
			Best = &Entry;
		}
	}

	return Best;
}

void UAnalytics_EventMapDataAsset::BuildAttributes(
	const FAnalytics_EventMapEntry& Entry,
	const FGameplayTag& SourceChannel,
	TArray<FSeam_AnalyticsAttr>& OutAttrs) const
{
	OutAttrs.Reset();

	// Optionally stamp the originating bus channel as a Tag attribute (PII-safe by type).
	if (Entry.bIncludeSourceChannelAttribute && SourceChannel.IsValid())
	{
		OutAttrs.Emplace(FName(TEXT("channel")), FSeam_NetValue::MakeTag(SourceChannel));
	}

	for (const FAnalytics_AttrExtractRule& Rule : Entry.Attributes)
	{
		if (Rule.AttributeKey.IsNone())
		{
			// A keyless attribute would be meaningless on the backend; skip it defensively.
			continue;
		}

		if (Rule.bUseSourceChannelAsValue)
		{
			OutAttrs.Emplace(Rule.AttributeKey, FSeam_NetValue::MakeTag(SourceChannel));
		}
		else if (Rule.LiteralValue.IsSet())
		{
			OutAttrs.Emplace(Rule.AttributeKey, Rule.LiteralValue);
		}
		// An unset literal with no derived source emits nothing (intentional no-op).
	}
}

#if WITH_EDITOR
EDataValidationResult UAnalytics_EventMapDataAsset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		const FAnalytics_EventMapEntry& Entry = Entries[Index];

		if (!Entry.SourceBusChannel.IsValid())
		{
			Context.AddError(FText::FromString(
				FString::Printf(TEXT("Event map entry %d has no SourceBusChannel."), Index)));
			Result = EDataValidationResult::Invalid;
		}

		if (!Entry.AnalyticsEvent.IsValid())
		{
			Context.AddError(FText::FromString(
				FString::Printf(TEXT("Event map entry %d has no AnalyticsEvent."), Index)));
			Result = EDataValidationResult::Invalid;
		}

		for (int32 AttrIndex = 0; AttrIndex < Entry.Attributes.Num(); ++AttrIndex)
		{
			if (Entry.Attributes[AttrIndex].AttributeKey.IsNone())
			{
				Context.AddWarning(FText::FromString(FString::Printf(
					TEXT("Event map entry %d attribute %d has an empty key and will be skipped."),
					Index, AttrIndex)));
			}
		}
	}

	return Result;
}
#endif
