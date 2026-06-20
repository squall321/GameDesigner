// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Music/Audio_MusicEventMapDataAsset.h"

UAudio_MusicEventMapDataAsset::UAudio_MusicEventMapDataAsset()
{
}

void UAudio_MusicEventMapDataAsset::GetDistinctChannels(TArray<FGameplayTag>& OutChannels) const
{
	OutChannels.Reset();
	for (const FAudio_MusicEventRule& Rule : Rules)
	{
		if (Rule.BusChannel.IsValid())
		{
			OutChannels.AddUnique(Rule.BusChannel);
		}
	}
}

void UAudio_MusicEventMapDataAsset::GatherMatchingRules(
	FGameplayTag Channel, bool bAllowChildMatch, TArray<FAudio_MusicEventRule>& OutRules) const
{
	for (const FAudio_MusicEventRule& Rule : Rules)
	{
		if (!Rule.BusChannel.IsValid())
		{
			continue;
		}

		// A rule fires when the broadcast Channel is the rule's tag, or (when permitted) a child of it.
		const bool bMatch = bAllowChildMatch
			? Channel.MatchesTag(Rule.BusChannel)
			: (Channel == Rule.BusChannel);

		if (bMatch)
		{
			OutRules.Add(Rule);
		}
	}
}

FName UAudio_MusicEventMapDataAsset::GetDataAssetType_Implementation() const
{
	return FName(TEXT("Audio_MusicEventMap"));
}
