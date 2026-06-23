// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Settings/InvUI_Settings.h"

UInvUI_Settings::UInvUI_Settings()
{
	// Defaults are the declared in-class initializers; nothing else needed here. The category
	// name is inherited from UDP_DeveloperSettings (groups under the "Plugins" settings section).
}

const UInvUI_Settings* UInvUI_Settings::Get()
{
	return GetDefault<UInvUI_Settings>();
}
