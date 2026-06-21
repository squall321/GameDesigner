// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Settings/WS_DeveloperSettings.h"

UWS_DeveloperSettings::UWS_DeveloperSettings()
{
	// All tunable defaults are set as field initializers in the header so the CDO carries them even
	// before any project ini override. Nothing magic to compute here.
}

const UWS_DeveloperSettings* UWS_DeveloperSettings::Get()
{
	// GetDefault on a Config UDeveloperSettings returns the ini-populated CDO. Never null in a running
	// game; callers still treat a theoretical null as "use field-initializer fallbacks".
	return GetDefault<UWS_DeveloperSettings>();
}
