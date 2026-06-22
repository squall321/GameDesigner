// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Capability/UPlat_FeatureGateSettings.h"

UPlat_FeatureGateSettings::UPlat_FeatureGateSettings()
{
	CategoryName = FName("Plugins");
}

const UPlat_FeatureGateSettings* UPlat_FeatureGateSettings::Get()
{
	return GetDefault<UPlat_FeatureGateSettings>();
}
