// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Settings/Prog_DeveloperSettings.h"

UProg_DeveloperSettings::UProg_DeveloperSettings()
{
	// Defaults are declared inline on the members; nothing further required here. The ctor exists so
	// the section name and category are registered before the config is loaded.
}

const UProg_DeveloperSettings* UProg_DeveloperSettings::Get()
{
	// GetDefault on a config UDeveloperSettings returns the CDO populated from the .ini — never null.
	const UProg_DeveloperSettings* Settings = GetDefault<UProg_DeveloperSettings>();
	check(Settings);
	return Settings;
}
