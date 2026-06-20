// Copyright DesignPatterns plugin. All Rights Reserved.

#include "SimGrid_DeveloperSettings.h"

USimGrid_DeveloperSettings::USimGrid_DeveloperSettings()
{
	// Defaults are declared inline on the UPROPERTYs; this ctor exists so the CDO is concrete and
	// GetMutableDefault/GetDefault resolve in non-configured contexts (tests, editor preview).
}

const USimGrid_DeveloperSettings* USimGrid_DeveloperSettings::Get()
{
	// GetDefault on a Config=Game DeveloperSettings returns the project-configured CDO.
	return GetDefault<USimGrid_DeveloperSettings>();
}
