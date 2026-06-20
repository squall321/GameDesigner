// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Settings/HUD_DeveloperSettings.h"
#include "HUD_NativeTags.h"

UHUD_DeveloperSettings::UHUD_DeveloperSettings()
{
	// Defensive defaults wired to this module's native anchor tags. A project overrides these in
	// DefaultGame.ini / Project Settings; these constructor values are the documented fallbacks so the
	// subsystems behave sanely even with an untouched config.
	MenuInputModeTag = HUDTags::InputMode_Menu;

	// Default to gameplay being the always-on base input layer.
	DefaultActiveLayers.AddTag(HUDTags::InputLayer_Gameplay);
}

const UHUD_DeveloperSettings* UHUD_DeveloperSettings::Get()
{
	// GetDefault on a UDeveloperSettings always returns the populated CDO (never null at runtime).
	return GetDefault<UHUD_DeveloperSettings>();
}
