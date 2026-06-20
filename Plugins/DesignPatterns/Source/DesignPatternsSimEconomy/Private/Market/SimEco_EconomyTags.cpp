// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Market/SimEco_EconomyTags.h"

/**
 * Single translation unit that DEFINES the market/economy-area native tags declared in
 * SimEco_EconomyTags.h. Defining them in exactly one .cpp satisfies the linker and guarantees the
 * hierarchy exists from engine startup.
 */
namespace SimEcoEconomyTags
{
	UE_DEFINE_GAMEPLAY_TAG(Persist,          "SimEco.Persist");
	UE_DEFINE_GAMEPLAY_TAG(Persist_Market,   "SimEco.Persist.Market");
	UE_DEFINE_GAMEPLAY_TAG(Service_SimClock, "DP.Service.SimClock");
}
