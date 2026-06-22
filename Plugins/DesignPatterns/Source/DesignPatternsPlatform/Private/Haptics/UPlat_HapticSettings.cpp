// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Haptics/UPlat_HapticSettings.h"

UPlat_HapticSettings::UPlat_HapticSettings()
{
	// CategoryName for the project-settings tree; the DisplayName comes from the UCLASS meta.
	CategoryName = FName("Plugins");
}

const UPlat_HapticSettings* UPlat_HapticSettings::Get()
{
	return GetDefault<UPlat_HapticSettings>();
}
