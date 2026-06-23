// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Settings/Analytics_TelemetrySettings.h"
#include "Tags/Analytics_TelemetryTags.h"

UAnalytics_TelemetrySettings::UAnalytics_TelemetrySettings()
{
	// Seed the observed bus channels from the module's documented default. DEFAULT values only;
	// projects override in config without coupling this module to any concrete channel tag.
	BreadcrumbBusChannelToObserve = AnalyticsTelemetryTags::DefaultTelemetryBusChannel;
	DashboardBusChannelToObserve = AnalyticsTelemetryTags::DefaultTelemetryBusChannel;
}

const UAnalytics_TelemetrySettings* UAnalytics_TelemetrySettings::Get()
{
	return GetDefault<UAnalytics_TelemetrySettings>();
}
