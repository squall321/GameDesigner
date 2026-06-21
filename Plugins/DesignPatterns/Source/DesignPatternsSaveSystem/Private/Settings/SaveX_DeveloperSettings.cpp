// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Settings/SaveX_DeveloperSettings.h"
#include "SaveX_ServiceKeys.h"

USaveX_DeveloperSettings::USaveX_DeveloperSettings()
{
	// Seed the slot-manager service key from the conventional SaveSystem key so an unedited project still
	// publishes/resolves the slot manager under DP.Service.Save.SlotManager. Projects may override in config.
	SlotManagerServiceTag = SaveX_ServiceKeys::SlotManager();

	// Remaining defaults live in the header member initializers so the CDO and an empty DefaultGame.ini agree.
}

const USaveX_DeveloperSettings* USaveX_DeveloperSettings::Get()
{
	return GetDefault<USaveX_DeveloperSettings>();
}
