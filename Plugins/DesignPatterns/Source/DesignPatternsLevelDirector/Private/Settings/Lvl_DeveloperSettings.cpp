// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Settings/Lvl_DeveloperSettings.h"

ULvl_DeveloperSettings::ULvl_DeveloperSettings()
{
	// Seed a sensible default band so a fresh project streams reasonably without authoring. These are
	// the documented defaults; projects override them in Project Settings. They are NOT magic numbers
	// embedded in director logic — the director reads them from this CDO at runtime.
	FLvl_DistanceBand Near;
	Near.BandName = TEXT("Default");
	Near.LoadWithinDistance = DefaultFallbackLoadDistance;
	Near.UnloadBeyondDistance = DefaultFallbackUnloadDistance;
	Near.bMakeVisibleWhenLoaded = true;
	DistanceBands.Add(Near);
}

const ULvl_DeveloperSettings* ULvl_DeveloperSettings::Get()
{
	// GetDefault is null only in pathological early-load; callers treat null as "use fallbacks".
	return GetDefault<ULvl_DeveloperSettings>();
}
