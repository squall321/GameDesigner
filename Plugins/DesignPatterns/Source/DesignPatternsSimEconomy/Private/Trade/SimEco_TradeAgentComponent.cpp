// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Trade/SimEco_TradeAgentComponent.h"
#include "Market/SimEco_MarketSubsystem.h"
#include "Market/SimEco_Market.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"
#include "GameFramework/Actor.h"

USimEco_TradeAgentComponent::USimEco_TradeAgentComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	// This component itself carries no replicated state; it only routes RPCs. Replication must still
	// be enabled so its Server/Client RPCs are routed for the owning connection.
	SetIsReplicatedByDefault(true);
}

bool USimEco_TradeAgentComponent::OwnerHasAuthority() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}

USimEco_MarketSubsystem* USimEco_TradeAgentComponent::ResolveMarket() const
{
	// Always resolve from THIS object's world — the server never trusts a client-named market.
	return FDP_SubsystemStatics::GetWorldSubsystem<USimEco_MarketSubsystem>(this);
}

void USimEco_TradeAgentComponent::RequestPlaceOrder(FGameplayTag CommodityTag, ESimEco_OrderSide Side, double Quantity, double LimitPrice)
{
	if (!CommodityTag.IsValid() || Quantity <= 0.0)
	{
		return;
	}

	if (OwnerHasAuthority())
	{
		// On the server (incl. standalone/listen host's local player) call the market directly.
		if (USimEco_MarketSubsystem* Market = ResolveMarket())
		{
			const double ClampedQty = FMath::Clamp(Quantity, 0.0, FMath::Max(0.0, MaxOrderQuantity));
			FSimEco_Order Order(CommodityTag, Side, ClampedQty, LimitPrice);
			const FSimEco_OrderReceipt Receipt = Market->PlaceOrder_Implementation(Order);
			OnOrderConfirmed.Broadcast(Receipt);
		}
		return;
	}

	// Remote client: route intent to the server.
	Server_PlaceOrder(CommodityTag, Side, Quantity, LimitPrice);
}

bool USimEco_TradeAgentComponent::Server_PlaceOrder_Validate(FGameplayTag CommodityTag, ESimEco_OrderSide Side, double Quantity, double LimitPrice)
{
	// Reject obviously malformed input before execution (cheap anti-cheat / anti-garbage gate).
	if (!CommodityTag.IsValid())
	{
		return false;
	}
	if (!(Quantity > 0.0) || !FMath::IsFinite(Quantity))
	{
		return false;
	}
	if (!FMath::IsFinite(LimitPrice) || LimitPrice < 0.0)
	{
		return false;
	}
	return true;
}

void USimEco_TradeAgentComponent::Server_PlaceOrder_Implementation(FGameplayTag CommodityTag, ESimEco_OrderSide Side, double Quantity, double LimitPrice)
{
	// AUTHORITY: this body runs only on the server. Re-derive everything server-side.
	USimEco_MarketSubsystem* Market = ResolveMarket();
	FSimEco_OrderReceipt Receipt;
	if (Market)
	{
		// Server re-clamps the quantity to the agent's allowed bound; never trusts the raw value.
		const double ClampedQty = FMath::Clamp(Quantity, 0.0, FMath::Max(0.0, MaxOrderQuantity));
		if (ClampedQty > 0.0)
		{
			FSimEco_Order Order(CommodityTag, Side, ClampedQty, FMath::Max(0.0, LimitPrice));
			Receipt = Market->PlaceOrder_Implementation(Order);
		}
	}

	// Deliver the outcome back to the requesting client.
	Client_OrderConfirmed(Receipt);
}

void USimEco_TradeAgentComponent::Client_OrderConfirmed_Implementation(FSimEco_OrderReceipt Receipt)
{
	OnOrderConfirmed.Broadcast(Receipt);
}
