// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Settings/Loc_DeveloperSettings.h"

ULoc_DeveloperSettings::ULoc_DeveloperSettings()
{
	// Defaults are declared inline on the UPROPERTYs above; this ctor exists so the CDO is a concrete
	// object designers can edit. No additional construction-time work is required.
}

const ULoc_DeveloperSettings* ULoc_DeveloperSettings::Get()
{
	// GetDefault<T>() returns the config-backed CDO. It is null only in pathological early-load contexts;
	// every caller treats a null return as "use documented defensive fallbacks".
	return GetDefault<ULoc_DeveloperSettings>();
}
