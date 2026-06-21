// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "Auction/SimEco_AuctionTypes.h"
#include "SimEco_AuctionReplicationProxy.generated.h"

/** Broadcast (server + clients) after the replicated lot summary changes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSimEco_OnAuctionLotsChanged, ASimEco_AuctionReplicationProxy*, Proxy);

/**
 * Replicated carrier for the PUBLIC auction board (the list of lot summaries).
 *
 * The auction subsystem is never replicated and holds the server-only escrow ledger (who staged which
 * items / currency). It pushes the public, non-sensitive lot summaries into THIS AInfo (one per world,
 * server-spawned under authority). Clients receive the fast-array delta and re-broadcast it locally;
 * UI reads lots from here. Escrow internals never leave the server.
 *
 * bReplicates + bAlwaysRelevant; referenced from a replicated UPROPERTY on the subsystem (held weakly)
 * and resolvable by clients off the world.
 */
UCLASS(NotPlaceable, Transient)
class DESIGNPATTERNSSIMECONOMY_API ASimEco_AuctionReplicationProxy : public AInfo
{
	GENERATED_BODY()

public:
	ASimEco_AuctionReplicationProxy();

	//~ Begin AActor
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PostInitProperties() override;
	//~ End AActor

	/** AUTHORITY ONLY: upsert a public lot summary (add or replace by LotId), marking it dirty. */
	void UpsertLot(const FSimEco_AuctionLot& Lot);

	/** AUTHORITY ONLY: remove the public summary for LotId (settled/cancelled/expired and reaped). */
	void RemoveLot(int32 LotId);

	/** Copy every replicated lot summary out for UI. */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Auction")
	void GetAllLots(TArray<FSimEco_AuctionLot>& OutLots) const { OutLots = LotSummaries.Lots; }

	/** Find a lot summary by id (read-only; client-safe). Returns nullptr if absent. */
	const FSimEco_AuctionLot* FindLot(int32 LotId) const;

	/** Fired (server + clients) after the lot summary changes. */
	UPROPERTY(BlueprintAssignable, Category = "SimEconomy|Auction")
	FSimEco_OnAuctionLotsChanged OnAuctionLotsChanged;

	/** Called by the fast-array callbacks on clients. */
	void HandleReplicatedChange();

private:
	/** Delta-replicated public lot summaries. */
	UPROPERTY(Replicated)
	FSimEco_AuctionLotArray LotSummaries;

	/** Mutable find by id (server side). */
	int32 FindLotIndex(int32 LotId) const;
};
