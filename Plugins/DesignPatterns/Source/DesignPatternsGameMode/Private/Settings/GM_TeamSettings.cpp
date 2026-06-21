// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Settings/GM_TeamSettings.h"

UGM_TeamSettings::UGM_TeamSettings()
{
	CategoryName = TEXT("Plugins");
}

const UGM_TeamSettings* UGM_TeamSettings::Get()
{
	// GetDefault on a UDeveloperSettings is guaranteed non-null for a registered settings class.
	return GetDefault<UGM_TeamSettings>();
}
