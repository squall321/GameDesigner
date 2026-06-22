// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Occlusion/Audio_OcclusionSettings.h"

UAudio_OcclusionSettings::UAudio_OcclusionSettings()
{
	// Defaults are declared inline on the UPROPERTYs; constructor exists for future config migration.
}

const UAudio_OcclusionSettings* UAudio_OcclusionSettings::Get()
{
	return GetDefault<UAudio_OcclusionSettings>();
}
