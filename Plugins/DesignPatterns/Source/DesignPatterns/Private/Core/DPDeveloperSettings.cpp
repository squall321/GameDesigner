// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Core/DPDeveloperSettings.h"

UDP_DeveloperSettings::UDP_DeveloperSettings()
{
	CategoryName = FName("Plugins");
	SectionName = FName("Design Patterns");
}

const UDP_DeveloperSettings* UDP_DeveloperSettings::Get()
{
	return GetDefault<UDP_DeveloperSettings>();
}
