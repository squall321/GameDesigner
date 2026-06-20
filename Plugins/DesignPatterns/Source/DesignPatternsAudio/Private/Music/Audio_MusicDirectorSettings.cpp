// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Music/Audio_MusicDirectorSettings.h"

UAudio_MusicDirectorSettings::UAudio_MusicDirectorSettings()
{
	// Defaults are declared inline on the UPROPERTYs; config overrides them per-project.
}

const UAudio_MusicDirectorSettings& UAudio_MusicDirectorSettings::GetChecked()
{
	// GetDefault never returns null for a registered UDeveloperSettings; the call is here so the
	// director has a single, audited access point for all music tunables.
	const UAudio_MusicDirectorSettings* Settings = GetDefault<UAudio_MusicDirectorSettings>();
	check(Settings);
	return *Settings;
}
