// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Settings/Tut_DeveloperSettings.h"

UTut_DeveloperSettings::UTut_DeveloperSettings()
{
	// Default the tutorial input mode to the conventional shared input-mode tag. A project can repoint it.
	// (A tag default, not a gameplay magic number.)
	DefaultTutorialInputModeTag =
		FGameplayTag::RequestGameplayTag(FName(TEXT("DP.Input.Mode.Tutorial")), /*ErrorIfNotFound=*/false);
}

const UTut_DeveloperSettings* UTut_DeveloperSettings::Get()
{
	return GetDefault<UTut_DeveloperSettings>();
}
