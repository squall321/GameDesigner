// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Pricing/SimEco_PricingTags.h"

/**
 * Single translation unit DEFINING the deep-economy area native tags declared in SimEco_PricingTags.h.
 * Defining each exactly once satisfies the linker and guarantees the hierarchy exists from startup.
 *
 * Bus channels nest under "DP.Bus.Eco" (a child of the core DP.Bus root) so a listener on DP.Bus.Eco
 * receives every economy notification. Service keys nest under "DP.Service.Eco". Persistence kinds
 * nest under "SimEco.Persist" (the existing SimEcoEconomyTags::Persist root).
 */
namespace SimEcoPricingTags
{
	UE_DEFINE_GAMEPLAY_TAG(Bus_MerchantTrade, "DP.Bus.Eco.MerchantTrade");
	UE_DEFINE_GAMEPLAY_TAG(Bus_AuctionChanged, "DP.Bus.Eco.AuctionChanged");
	UE_DEFINE_GAMEPLAY_TAG(Bus_TradeChanged,   "DP.Bus.Eco.TradeChanged");
	UE_DEFINE_GAMEPLAY_TAG(Bus_BankChanged,    "DP.Bus.Eco.BankChanged");
	UE_DEFINE_GAMEPLAY_TAG(Bus_RewardPaid,     "DP.Bus.Eco.RewardPaid");
	UE_DEFINE_GAMEPLAY_TAG(Bus_EconomicEvent,  "DP.Bus.Eco.EconomicEvent");

	UE_DEFINE_GAMEPLAY_TAG(Service_PriceQuote, "DP.Service.Eco.PriceQuote");
	UE_DEFINE_GAMEPLAY_TAG(Service_Auction,    "DP.Service.Eco.Auction");
	UE_DEFINE_GAMEPLAY_TAG(Service_RewardSink, "DP.Service.Eco.RewardSink");

	UE_DEFINE_GAMEPLAY_TAG(Persist_Auction,    "SimEco.Persist.Auction");
	UE_DEFINE_GAMEPLAY_TAG(Persist_Events,     "SimEco.Persist.Events");

	UE_DEFINE_GAMEPLAY_TAG(Rep_Tier,           "Reputation.Tier");
}
