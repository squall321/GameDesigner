// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

/**
 * Native (C++-defined) anchor tags for the DesignPatternsSimEconomy module.
 *
 * Mirrors the core plugin convention (see DPNativeTags): only ROOT/anchor tags are declared
 * here so the tag hierarchy is guaranteed to exist at startup. Concrete commodities
 * (DP.SimEco.Commodity.Wood), facilities (DP.SimEco.Facility.Sawmill) and the per-event bus
 * channels are authored as children of these roots — either in project tag config or via a
 * USimEco_CommodityDef / USimEco_ProcessDef DataTag.
 *
 * The three bus channels are children of the core DP.Bus root so a listener registered on
 * DP.Bus (hierarchy match) still receives economy traffic, while a focused listener can scope
 * to exactly DP.Bus.SimEco.PriceChanged.
 */
namespace SimEcoNativeTags
{
	// --- Identity roots (children authored by content) ---

	/** Root for every commodity identity tag (e.g. DP.SimEco.Commodity.Grain). */
	DESIGNPATTERNSSIMECONOMY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Commodity);

	/** Root for every facility-type tag a process can require (e.g. DP.SimEco.Facility.Sawmill). */
	DESIGNPATTERNSSIMECONOMY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Facility);

	// --- Message bus channels (children of DP.Bus) ---

	/** Broadcast once per authoritative economy fixed-step after all producers/consumers ran. */
	DESIGNPATTERNSSIMECONOMY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_TickCompleted);

	/** Broadcast by a market when a batch of trades clears at a price. */
	DESIGNPATTERNSSIMECONOMY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_TradeCleared);

	/** Broadcast by a market when a commodity's clearing price moves beyond the replication epsilon. */
	DESIGNPATTERNSSIMECONOMY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_PriceChanged);

	// NOTE: the sim-clock service-locator key lives in SimEcoEconomyTags::Service_SimClock
	// (owned by the market/economy-driver area) so the two areas never double-define the same tag.
}
