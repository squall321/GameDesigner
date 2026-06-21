// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

/**
 * Native anchor tags owned by the DEEP-ECONOMY area of DesignPatternsSimEconomy (pricing, merchants,
 * auction, P2P trade, bank, quest reward, economic events).
 *
 * Kept in a SEPARATE namespace from SimEcoNativeTags (commodity/settings) and SimEcoEconomyTags
 * (market/driver/save) so the three areas never collide on UE_DEFINE_GAMEPLAY_TAG. Bus channels are
 * children of the core DP.Bus root; service keys are children of DP.Service; persistence kinds are
 * children of SimEco.Persist.
 */
namespace SimEcoPricingTags
{
	// ---- Message-bus channels (children of DP.Bus; state-derived NOTIFICATIONS, never commands) ----

	/** A merchant buy/sell completed (DP.Bus.Eco.MerchantTrade). Payload: FSimEco_MerchantTradeMsg. */
	DESIGNPATTERNSSIMECONOMY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_MerchantTrade);

	/** An auction lot changed: listed / bid / bought-out / settled (DP.Bus.Eco.AuctionChanged). */
	DESIGNPATTERNSSIMECONOMY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_AuctionChanged);

	/** A P2P trade session changed state (DP.Bus.Eco.TradeChanged). */
	DESIGNPATTERNSSIMECONOMY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_TradeChanged);

	/** A bank operation completed: deposit / withdraw / interest / loan (DP.Bus.Eco.BankChanged). */
	DESIGNPATTERNSSIMECONOMY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_BankChanged);

	/** A reward was paid out (DP.Bus.Eco.RewardPaid). Payload: FSimEco_RewardPaidMsg. */
	DESIGNPATTERNSSIMECONOMY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_RewardPaid);

	/** An economic event began or ended (DP.Bus.Eco.EconomicEvent). Payload: FSimEco_EconomicEventMsg. */
	DESIGNPATTERNSSIMECONOMY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_EconomicEvent);

	// ---- Service-locator keys (children of DP.Service) ----

	/** Key under which the SimEconomy market adapter publishes ISeam_PriceQuote (DP.Service.Eco.PriceQuote). */
	DESIGNPATTERNSSIMECONOMY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_PriceQuote);

	/** Key under which the auction subsystem publishes itself (DP.Service.Eco.Auction). */
	DESIGNPATTERNSSIMECONOMY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_Auction);

	/** Key under which the quest-reward component publishes ISeam_RewardSink (DP.Service.Eco.RewardSink). */
	DESIGNPATTERNSSIMECONOMY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_RewardSink);

	// ---- Persistence kinds (children of SimEco.Persist) ----

	/** Persistence kind for the auction subsystem's lots + escrow ledger (SimEco.Persist.Auction). */
	DESIGNPATTERNSSIMECONOMY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Persist_Auction);

	/** Persistence kind for the economic-event subsystem's active events (SimEco.Persist.Events). */
	DESIGNPATTERNSSIMECONOMY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Persist_Events);

	// ---- Reputation tier anchors (read by the price/discount adapter; project authors children) ----

	/** Root for reputation tiers used by merchant discount tables (Reputation.Tier.*). */
	DESIGNPATTERNSSIMECONOMY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Rep_Tier);
}
