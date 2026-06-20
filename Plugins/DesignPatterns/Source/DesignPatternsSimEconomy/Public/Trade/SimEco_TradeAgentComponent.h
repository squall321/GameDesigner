// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Market/SimEco_Market.h"
#include "GameplayTagContainer.h"
#include "SimEco_TradeAgentComponent.generated.h"

class USimEco_MarketSubsystem;

/** Broadcast on the owning client after the server confirms an order receipt for this agent. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSimEco_OnOrderConfirmed, FSimEco_OrderReceipt, Receipt);

/**
 * Player-owned component that routes a player's trade INTENT to the authoritative market.
 *
 * Client intent must never call the market subsystem directly (its PlaceOrder is a server-authority
 * API, not an RPC). Instead the local player calls RequestPlaceOrder, which sends Server_PlaceOrder
 * (Reliable, WithValidation). The server re-derives the market from ITS world (never trusting a
 * client-named market), re-validates the order, then calls Market->PlaceOrder under authority.
 *
 * Put this on a player-owned actor (PlayerController / Pawn / PlayerState) so the RPC has a valid
 * owning connection.
 */
UCLASS(ClassGroup = (DesignPatternsSimEconomy), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSSIMECONOMY_API USimEco_TradeAgentComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USimEco_TradeAgentComponent();

	/**
	 * Client-side entry point: submit a trade order. On the server this calls the market directly;
	 * on a client it forwards to Server_PlaceOrder. Safe to call from player UI / input.
	 */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Trade")
	void RequestPlaceOrder(FGameplayTag CommodityTag, ESimEco_OrderSide Side, double Quantity, double LimitPrice = 0.0);

	/** Fired on the owning client once the server has processed an order this agent submitted. */
	UPROPERTY(BlueprintAssignable, Category = "SimEconomy|Trade")
	FSimEco_OnOrderConfirmed OnOrderConfirmed;

	/**
	 * Largest single-order quantity this agent may submit. A server-side sanity bound applied in
	 * validation so a compromised client cannot flood the book. A tunable, not a magic number.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimEconomy|Trade",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	double MaxOrderQuantity = 1000.0;

private:
	/**
	 * Server RPC carrying the player's order. WithValidation rejects malformed input before
	 * execution. The server re-derives the market from its own world and re-checks the order; it
	 * never trusts a client-named target.
	 */
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_PlaceOrder(FGameplayTag CommodityTag, ESimEco_OrderSide Side, double Quantity, double LimitPrice);

	/** Client RPC delivering the server's receipt back to the requesting client. */
	UFUNCTION(Client, Reliable)
	void Client_OrderConfirmed(FSimEco_OrderReceipt Receipt);

	/** Resolve the authoritative market for the server's world (never a client-supplied reference). */
	USimEco_MarketSubsystem* ResolveMarket() const;

	/** True if the owning actor currently has network authority (server). */
	bool OwnerHasAuthority() const;
};
