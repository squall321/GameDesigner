// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Settings/SimEco_DeveloperSettings.h"

USimEco_DeveloperSettings::USimEco_DeveloperSettings()
{
	// Defaults are declared inline on the members; nothing further required here. The ctor exists
	// so the section name and category are registered before the config is loaded.
}

const USimEco_DeveloperSettings* USimEco_DeveloperSettings::Get()
{
	// GetDefault on a config UDeveloperSettings returns the CDO populated from the .ini — never null.
	const USimEco_DeveloperSettings* Settings = GetDefault<USimEco_DeveloperSettings>();
	check(Settings);
	return Settings;
}
