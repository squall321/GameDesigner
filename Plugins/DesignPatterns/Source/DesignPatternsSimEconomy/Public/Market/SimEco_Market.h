// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "SimEco_Market.generated.h"

/** Side of a market order. */
UENUM(BlueprintType)
enum class ESimEco_OrderSide : uint8
{
	/** Agent wants to acquire the commodity (adds to demand). */
	Buy,
	/** Agent wants to dispose of the commodity (adds to supply). */
	Sell
};

/**
 * A single order submitted into a market before the next clearing.
 *
 * Orders are intents, not guarantees: the market aggregates all outstanding buy/sell quantity per
 * commodity and forms a clearing price; an individual order fills proportionally to liquidity.
 * Referenced commodity is a tag (never a hard pointer) so orders serialize trivially.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMECONOMY_API FSimEco_Order
{
	GENERATED_BODY()

	/** Commodity being traded (child of SimEco.Commodity / matches a USimEco_CommodityDef DataTag). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimEconomy|Market")
	FGameplayTag CommodityTag;

	/** Buy (demand) or Sell (supply). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimEconomy|Market")
	ESimEco_OrderSide Side = ESimEco_OrderSide::Buy;

	/** Desired quantity. Always strictly positive for a meaningful order. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimEconomy|Market",
		meta = (ClampMin = "0.0"))
	double Quantity = 0.0;

	/**
	 * Per-unit price limit. A buy fills only at or below this; a sell fills only at or above it.
	 * Zero/negative means "market order" (accept the clearing price unconditionally).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimEconomy|Market")
	double LimitPrice = 0.0;

	FSimEco_Order() = default;
	FSimEco_Order(const FGameplayTag& InCommodity, ESimEco_OrderSide InSide, double InQuantity, double InLimit = 0.0)
		: CommodityTag(InCommodity), Side(InSide), Quantity(InQuantity), LimitPrice(InLimit) {}

	bool IsValidOrder() const { return CommodityTag.IsValid() && Quantity > 0.0; }
};

/** Result of submitting an order: whether the market accepted it for the next clearing. */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMECONOMY_API FSimEco_OrderReceipt
{
	GENERATED_BODY()

	/** True if the order was queued for clearing (commodity known, quantity positive, server authority). */
	UPROPERTY(BlueprintReadOnly, Category = "SimEconomy|Market")
	bool bAccepted = false;

	/** The market's indicative price for the commodity at the moment of submission. */
	UPROPERTY(BlueprintReadOnly, Category = "SimEconomy|Market")
	double IndicativePrice = 0.0;

	/** Quantity actually queued (may be quantized for indivisible commodities). */
	UPROPERTY(BlueprintReadOnly, Category = "SimEconomy|Market")
	double QueuedQuantity = 0.0;
};

UINTERFACE(BlueprintType, MinimalAPI)
class USimEco_Market : public UInterface
{
	GENERATED_BODY()
};

/**
 * Read/authority seam for a price-forming market. Implemented by USimEco_MarketSubsystem; consumed
 * by trade agents, facilities and UI through TScriptInterface<ISimEco_Market> so nothing depends on
 * the concrete subsystem type.
 *
 * Read methods (GetPrice/GetSupply/GetDemand) are const and safe to call anywhere (they read the
 * server-side book on the server and, on clients, the replicated price summary). PlaceOrder and
 * ClearMarket are SERVER-AUTHORITY APIs — they are NOT RPCs; client intent must be routed through a
 * player-owned component's Server_PlaceOrder which forwards to PlaceOrder only on the server.
 */
class DESIGNPATTERNSSIMECONOMY_API ISimEco_Market
{
	GENERATED_BODY()

public:
	/** Current clearing/indicative price per unit of CommodityTag (0 if the commodity is unknown). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|SimEco|Market")
	double GetPrice(FGameplayTag CommodityTag) const;

	/** Aggregate sell-side quantity currently queued for the next clearing. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|SimEco|Market")
	double GetSupply(FGameplayTag CommodityTag) const;

	/** Aggregate buy-side quantity currently queued for the next clearing. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|SimEco|Market")
	double GetDemand(FGameplayTag CommodityTag) const;

	/**
	 * Submit an order. SERVER-AUTHORITY ONLY: implementers MUST early-return a rejected receipt when
	 * called without world authority (this is not an RPC). Returns a receipt describing acceptance.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|SimEco|Market")
	FSimEco_OrderReceipt PlaceOrder(const FSimEco_Order& Order);

	/**
	 * Run one market-clearing pass: form a new price per commodity from queued supply/demand, then
	 * drain the order book. Called by the economy fixed-step driver. SERVER-AUTHORITY ONLY.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|SimEco|Market")
	void ClearMarket();
};
