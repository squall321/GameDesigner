// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Auction/SimEco_AuctionSubsystem.h"
#include "GameplayTagContainer.h"
#include "SimEco_AuctionClientComponent.generated.h"

/** Broadcast on the owning client once the server resolves an auction request from this component. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSimEco_OnAuctionResult, ESimEco_AuctionResult, Result, int32, LotId);

/**
 * PLAYER-OWNED component routing a player's auction INTENT (list / bid / buyout / cancel) to the
 * authoritative auction subsystem.
 *
 * The auction subsystem's API is authority-driven (not RPCs). This component is the client-facing
 * bridge: it issues validated Server_* RPCs; the server re-resolves the subsystem from ITS world and
 * calls the authority API on the OWNING player as seller/bidder (it never trusts a client-named actor —
 * the seller/bidder is always GetOwner()). Place on a player-owned actor so the RPCs route.
 */
UCLASS(ClassGroup = (DesignPatternsSimEconomy), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSSIMECONOMY_API USimEco_AuctionClientComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USimEco_AuctionClientComponent();

	/** Client entry: list an item the owning player holds. */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Auction")
	void RequestList(FGameplayTag ItemTag, int32 Quantity, FGameplayTag CurrencyTag,
		int64 MinBid, int64 BuyoutPrice, int32 DurationDays);

	/** Client entry: bid on a lot. */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Auction")
	void RequestBid(int32 LotId, int64 Amount);

	/** Client entry: buy a lot outright. */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Auction")
	void RequestBuyout(int32 LotId);

	/** Client entry: cancel an unbid lot the owning player listed. */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Auction")
	void RequestCancel(int32 LotId);

	/** Fired on the owning client once the server resolves a request. */
	UPROPERTY(BlueprintAssignable, Category = "SimEconomy|Auction")
	FSimEco_OnAuctionResult OnAuctionResult;

	/** Server cap on a single lot quantity (anti-garbage). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimEconomy|Auction", meta = (ClampMin = "1"))
	int32 MaxListQuantity = 999;

private:
	bool OwnerHasAuthority() const;
	USimEco_AuctionSubsystem* ResolveAuction() const;

	UFUNCTION(Server, Reliable, WithValidation)
	void Server_List(FGameplayTag ItemTag, int32 Quantity, FGameplayTag CurrencyTag,
		int64 MinBid, int64 BuyoutPrice, int32 DurationDays);

	UFUNCTION(Server, Reliable, WithValidation)
	void Server_Bid(int32 LotId, int64 Amount);

	UFUNCTION(Server, Reliable, WithValidation)
	void Server_Buyout(int32 LotId);

	UFUNCTION(Server, Reliable, WithValidation)
	void Server_Cancel(int32 LotId);

	UFUNCTION(Client, Reliable)
	void Client_AuctionResult(ESimEco_AuctionResult Result, int32 LotId);
};
