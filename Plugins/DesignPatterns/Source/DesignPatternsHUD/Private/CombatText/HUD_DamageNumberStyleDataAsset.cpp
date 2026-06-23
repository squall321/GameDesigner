// Copyright DesignPatterns plugin. All Rights Reserved.

#include "CombatText/HUD_DamageNumberStyleDataAsset.h"

const FHUD_DamageNumberStyleRow& UHUD_DamageNumberStyleDataAsset::ResolveStyle(const FGameplayTag& Classification) const
{
	if (!Classification.IsValid())
	{
		return DefaultStyle;
	}

	// Prefer an exact row; otherwise the most-specific matching parent (longest matching tag string), so a
	// project can author a broad "Crit" style and override a specific "Crit.Headshot" — mirrors the
	// minimap's icon-resolution policy.
	const FHUD_DamageNumberStyleRow* Best = nullptr;
	int32 BestDepth = -1;
	for (const FHUD_DamageNumberStyleRow& Row : StyleRows)
	{
		if (!Row.ClassificationTag.IsValid())
		{
			continue;
		}
		if (Row.ClassificationTag == Classification)
		{
			return Row; // exact match wins immediately
		}
		if (Classification.MatchesTag(Row.ClassificationTag))
		{
			const int32 Depth = Row.ClassificationTag.ToString().Len();
			if (Depth > BestDepth)
			{
				BestDepth = Depth;
				Best = &Row;
			}
		}
	}
	return Best ? *Best : DefaultStyle;
}
