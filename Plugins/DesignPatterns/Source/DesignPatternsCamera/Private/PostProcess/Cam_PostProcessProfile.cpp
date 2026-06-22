// Copyright DesignPatterns plugin. All Rights Reserved.

#include "PostProcess/Cam_PostProcessProfile.h"

UCam_PostProcessProfile::UCam_PostProcessProfile()
{
}

bool UCam_PostProcessProfile::ResolveForMode(FGameplayTag ModeTag, FCam_PostProcessSettings& OutSettings) const
{
	if (!ModeTag.IsValid())
	{
		return false;
	}

	// Most-specific (deepest) ancestor-or-equal route wins, mirroring the shake library's matching.
	const FCam_PostProcessRoute* Best = nullptr;
	int32 BestDepth = -1;
	for (const FCam_PostProcessRoute& Route : Routes)
	{
		if (!Route.ModeTag.IsValid())
		{
			continue;
		}
		if (ModeTag == Route.ModeTag || ModeTag.MatchesTag(Route.ModeTag))
		{
			const int32 Depth = Route.ModeTag.ToString().Len();
			if (Depth > BestDepth)
			{
				BestDepth = Depth;
				Best = &Route;
			}
		}
	}

	if (Best)
	{
		OutSettings = Best->Settings;
		return true;
	}
	return false;
}

FName UCam_PostProcessProfile::GetDataAssetType_Implementation() const
{
	return FName("Cam_PostProcessProfile");
}
