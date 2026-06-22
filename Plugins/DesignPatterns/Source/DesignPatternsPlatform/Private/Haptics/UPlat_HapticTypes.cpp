// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Haptics/UPlat_HapticTypes.h"
#include "Plat_NativeTags.h"

bool UPlat_HapticEffectSet::FindEffect(FGameplayTag EffectTag, FPlat_HapticEffect& Out) const
{
	if (!EffectTag.IsValid())
	{
		return false;
	}

	// Exact match first.
	for (const FPlat_HapticEffect& Effect : Effects)
	{
		if (Effect.EffectTag == EffectTag)
		{
			Out = Effect;
			return true;
		}
	}

	// Hierarchical fallback: the closest parent-tag match (e.g. ...Hit.Heavy falls back to ...Hit). We
	// pick the most specific parent among matching rows by tag depth.
	const FPlat_HapticEffect* Best = nullptr;
	int32 BestDepth = -1;
	for (const FPlat_HapticEffect& Effect : Effects)
	{
		if (Effect.EffectTag.IsValid() && EffectTag.MatchesTag(Effect.EffectTag))
		{
			const int32 Depth = Effect.EffectTag.ToString().Len();
			if (Depth > BestDepth)
			{
				BestDepth = Depth;
				Best = &Effect;
			}
		}
	}
	if (Best)
	{
		Out = *Best;
		return true;
	}
	return false;
}

FName UPlat_HapticEffectSet::GetDataAssetType_Implementation() const
{
	// Group all haptic banks under a single asset-manager type bucket.
	return FName("Plat_HapticEffectSet");
}
