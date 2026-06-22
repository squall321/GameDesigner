// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Display/UPlat_DisplaySettings.h"

UPlat_DisplaySettings::UPlat_DisplaySettings()
{
	CategoryName = FName("Plugins");
}

const UPlat_DisplaySettings* UPlat_DisplaySettings::Get()
{
	return GetDefault<UPlat_DisplaySettings>();
}
