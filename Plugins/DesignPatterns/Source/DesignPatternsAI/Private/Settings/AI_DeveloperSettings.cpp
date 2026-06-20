// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Settings/AI_DeveloperSettings.h"

UAI_DeveloperSettings::UAI_DeveloperSettings()
{
	// Section name shown under Project Settings → Plugins.
	CategoryName = FName("Plugins");
}

const UAI_DeveloperSettings* UAI_DeveloperSettings::Get()
{
	// GetDefault never returns null for a UDeveloperSettings CDO in a configured build.
	return GetDefault<UAI_DeveloperSettings>();
}
