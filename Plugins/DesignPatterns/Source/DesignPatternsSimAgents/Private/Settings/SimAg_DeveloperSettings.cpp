// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Settings/SimAg_DeveloperSettings.h"
#include "UObject/UObjectGlobals.h"

USimAg_DeveloperSettings::USimAg_DeveloperSettings()
{
	// Section name shown in Project Settings (under Plugins). Defaults above are the genre-neutral
	// tunables; projects override them in DefaultGame.ini via this DeveloperSettings.
}

const USimAg_DeveloperSettings* USimAg_DeveloperSettings::Get()
{
	// GetDefault on a Config DeveloperSettings returns the CDO populated from the project's ini —
	// the canonical, always-valid source of these tunables at runtime and in the editor.
	return GetDefault<USimAg_DeveloperSettings>();
}
