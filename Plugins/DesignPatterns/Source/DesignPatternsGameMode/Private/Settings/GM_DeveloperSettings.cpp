// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Settings/GM_DeveloperSettings.h"
#include "DesignPatternsGameModeModule.h"

UGM_DeveloperSettings::UGM_DeveloperSettings()
{
	// Default the neutral score bucket to the module's native DP.Score.Default tag. Done in the ctor
	// (not as an inline initializer) so the native tag is resolved when the CDO is constructed.
	DefaultScoreBucket = GameModeNativeTags::Score_Default;
}

const UGM_DeveloperSettings* UGM_DeveloperSettings::Get()
{
	// GetDefault never returns null for a registered UDeveloperSettings; the CDO holds config values.
	return GetDefault<UGM_DeveloperSettings>();
}
