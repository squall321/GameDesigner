// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Settings/SimGrid_FeatureSettings.h"

USimGrid_FeatureSettings::USimGrid_FeatureSettings()
{
	// Default the service keys to stable anchors under DP.Service.SimGrid so a project that does not
	// override them still gets working registrations. RequestGameplayTag with ErrorIfNotFound=false so
	// a project whose tag table lacks these (unlikely — they are added by SimGrid native tags) degrades
	// to an unset tag (the registering code logs and skips) rather than asserting at CDO construction.
	ZoneCarrierServiceTag = FGameplayTag::RequestGameplayTag(
		FName("DP.Service.SimGrid.ZoneCarrier"), /*ErrorIfNotFound*/ false);
	FogCarrierServiceTag = FGameplayTag::RequestGameplayTag(
		FName("DP.Service.SimGrid.FogCarrier"), /*ErrorIfNotFound*/ false);
	LayeredTileProviderServiceTag = FGameplayTag::RequestGameplayTag(
		FName("DP.Service.SimGrid.LayeredTileProvider"), /*ErrorIfNotFound*/ false);
	HeightProviderServiceTag = FGameplayTag::RequestGameplayTag(
		FName("DP.Service.SimGrid.HeightProvider"), /*ErrorIfNotFound*/ false);
}

const USimGrid_FeatureSettings* USimGrid_FeatureSettings::Get()
{
	return GetDefault<USimGrid_FeatureSettings>();
}
