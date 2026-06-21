// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Events/SimEco_EconomicEventDef.h"

USimEco_EconomicEventDef::USimEco_EconomicEventDef()
{
}

FName USimEco_EconomicEventDef::GetDataAssetType_Implementation() const
{
	return FName(TEXT("SimEco_EconomicEvent"));
}
