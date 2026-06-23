// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Status/HUD_StatusBarStyleDataAsset.h"

bool UHUD_StatusBarStyleDataAsset::ResolveStyle(const FGameplayTag& StatusTag, TSoftObjectPtr<UTexture2D>& OutIcon, FLinearColor& OutTint) const
{
	OutIcon = TSoftObjectPtr<UTexture2D>();
	OutTint = DefaultTint;

	if (!StatusTag.IsValid())
	{
		return false;
	}

	const FHUD_StatusIconRow* Best = nullptr;
	int32 BestDepth = -1;
	for (const FHUD_StatusIconRow& Row : Rows)
	{
		if (!Row.StatusTag.IsValid())
		{
			continue;
		}
		if (Row.StatusTag == StatusTag)
		{
			OutIcon = Row.Icon;
			OutTint = Row.Tint;
			return true;
		}
		if (StatusTag.MatchesTag(Row.StatusTag))
		{
			const int32 Depth = Row.StatusTag.ToString().Len();
			if (Depth > BestDepth)
			{
				BestDepth = Depth;
				Best = &Row;
			}
		}
	}

	if (Best)
	{
		OutIcon = Best->Icon;
		OutTint = Best->Tint;
		return true;
	}
	return false;
}
