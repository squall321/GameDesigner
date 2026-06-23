// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Subtitle/Loc_SpeakerStyleDataAsset.h"

bool ULoc_SpeakerStyleDataAsset::FindStyle(FGameplayTag Speaker, FLoc_SpeakerStyle& Out) const
{
	if (Speaker.IsValid())
	{
		// Exact match first.
		if (const FLoc_SpeakerStyle* Exact = Styles.Find(Speaker))
		{
			Out = *Exact;
			return true;
		}

		// Most-specific ancestor match.
		const FLoc_SpeakerStyle* Best = nullptr;
		int32 BestDepth = -1;
		for (const TPair<FGameplayTag, FLoc_SpeakerStyle>& Pair : Styles)
		{
			if (!Pair.Key.IsValid() || !Speaker.MatchesTag(Pair.Key))
			{
				continue;
			}
			int32 Depth = 0;
			for (TCHAR Ch : Pair.Key.ToString())
			{
				if (Ch == TEXT('.'))
				{
					++Depth;
				}
			}
			if (Depth > BestDepth)
			{
				BestDepth = Depth;
				Best = &Pair.Value;
			}
		}
		if (Best)
		{
			Out = *Best;
			return true;
		}
	}

	// No specific entry: use the default.
	Out = DefaultStyle;
	return false;
}

FName ULoc_SpeakerStyleDataAsset::GetDataAssetType_Implementation() const
{
	return FName(TEXT("Loc_SpeakerStyles"));
}
