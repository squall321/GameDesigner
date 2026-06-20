// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Settings/SimGrid_DeveloperSettings.h"

USimGrid_DeveloperSettings::USimGrid_DeveloperSettings()
{
	// Default the service tag to the SimGrid tile-provider key so a project gets a working
	// registration out of the box; projects may rebind it in DefaultGame.ini.
	TileProviderServiceTag = FGameplayTag::RequestGameplayTag(
		FName(TEXT("DP.Service.SimGrid.TileProvider")), /*ErrorIfNotFound*/ false);
}

const USimGrid_DeveloperSettings* USimGrid_DeveloperSettings::Get()
{
	return GetDefault<USimGrid_DeveloperSettings>();
}
