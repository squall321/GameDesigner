// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Settings/Analytics_DeveloperSettings.h"
#include "DesignPatternsAnalyticsModule.h"

UAnalytics_DeveloperSettings::UAnalytics_DeveloperSettings()
{
	// Seed the observed bus channel from the module's documented default. This is a DEFAULT
	// value only; projects override it in config without coupling this module to any concrete
	// channel tag owned by another module.
	BusChannelToObserve = AnalyticsNativeTags::DefaultObservedBusChannel;
}

const UAnalytics_DeveloperSettings* UAnalytics_DeveloperSettings::Get()
{
	// GetDefault is safe to call after CDOs exist; returns the config-backed CDO.
	return GetDefault<UAnalytics_DeveloperSettings>();
}
