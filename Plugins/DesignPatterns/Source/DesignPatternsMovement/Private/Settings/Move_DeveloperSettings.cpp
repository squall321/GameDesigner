// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Settings/Move_DeveloperSettings.h"

UMove_DeveloperSettings::UMove_DeveloperSettings()
{
	// All members carry sane in-class defaults (see header). The constructor is intentionally empty;
	// projects override any field in DefaultGame.ini under [/Script/DesignPatternsMovement.Move_DeveloperSettings].
}

const UMove_DeveloperSettings* UMove_DeveloperSettings::Get()
{
	return GetDefault<UMove_DeveloperSettings>();
}
