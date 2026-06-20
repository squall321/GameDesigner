// Copyright DesignPatterns plugin. All Rights Reserved.

#include "DesignPatternsSimEconomyModule.h"
#include "Modules/ModuleManager.h"
#include "Core/DPLog.h"

namespace SimEcoNativeTags
{
	// Identity roots. Concrete commodities/facilities are authored as child tags by content.
	UE_DEFINE_GAMEPLAY_TAG(Commodity, "DP.SimEco.Commodity");
	UE_DEFINE_GAMEPLAY_TAG(Facility,  "DP.SimEco.Facility");

	// Bus channels: children of the core DP.Bus root so hierarchy matching reaches them.
	UE_DEFINE_GAMEPLAY_TAG(Bus_TickCompleted, "DP.Bus.SimEco.TickCompleted");
	UE_DEFINE_GAMEPLAY_TAG(Bus_TradeCleared,  "DP.Bus.SimEco.TradeCleared");
	UE_DEFINE_GAMEPLAY_TAG(Bus_PriceChanged,  "DP.Bus.SimEco.PriceChanged");
}

class FDesignPatternsSimEconomyModule : public IModuleInterface
{
public:
	virtual void StartupModule() override { UE_LOG(LogDP, Log, TEXT("DesignPatternsSimEconomy module started.")); }
	virtual void ShutdownModule() override { UE_LOG(LogDP, Log, TEXT("DesignPatternsSimEconomy module shut down.")); }
};

IMPLEMENT_MODULE(FDesignPatternsSimEconomyModule, DesignPatternsSimEconomy)
