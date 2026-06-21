// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "GameplayTagContainer.h"
#include "Identity/Seam_EntityId.h"
#include "SimEco_AuctionTypes.generated.h"

class ASimEco_AuctionReplicationProxy;

/** Lifecycle state of an auction lot. */
UENUM(BlueprintType)
enum class ESimEco_AuctionState : uint8
{
	/** Listed and accepting bids / buyout. */
	Active,
	/** Sold (by buyout or at settlement to the high bidder). */
	Sold,
	/** Expired with no qualifying bid; goods returned to the seller. */
	Expired,
	/** Cancelled by the seller (only allowed before any bid). */
	Cancelled
};

/**
 * Public, replicated summary of one auction lot (NO escrow internals — those stay server-side in the
 * subsystem ledger). Carried in a fast-array so a single bid delta-replicates. Identities are
 * FSeam_EntityId, never raw actor pointers, so the summary is connection-agnostic.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMECONOMY_API FSimEco_AuctionLot : public FFastArraySerializerItem
{
	GENERATED_BODY()

	/** Stable lot id (server-assigned, monotonic). */
	UPROPERTY(BlueprintReadOnly, Category = "SimEconomy|Auction")
	int32 LotId = INDEX_NONE;

	/** The item being auctioned. */
	UPROPERTY(BlueprintReadOnly, Category = "SimEconomy|Auction")
	FGameplayTag ItemTag;

	/** Quantity in the lot. */
	UPROPERTY(BlueprintReadOnly, Category = "SimEconomy|Auction")
	int32 Quantity = 1;

	/** Currency the lot is priced in. */
	UPROPERTY(BlueprintReadOnly, Category = "SimEconomy|Auction")
	FGameplayTag CurrencyTag;

	/** Minimum opening bid. */
	UPROPERTY(BlueprintReadOnly, Category = "SimEconomy|Auction")
	int64 MinBid = 0;

	/** Current highest bid (0 if none). */
	UPROPERTY(BlueprintReadOnly, Category = "SimEconomy|Auction")
	int64 CurrentBid = 0;

	/** Instant-buy price (0 = no buyout). */
	UPROPERTY(BlueprintReadOnly, Category = "SimEconomy|Auction")
	int64 BuyoutPrice = 0;

	/** Seller identity. */
	UPROPERTY(BlueprintReadOnly, Category = "SimEconomy|Auction")
	FSeam_EntityId SellerId;

	/** Current high bidder identity (invalid if no bid). */
	UPROPERTY(BlueprintReadOnly, Category = "SimEconomy|Auction")
	FSeam_EntityId HighBidderId;

	/** Sim-clock day the lot settles/expires on. */
	UPROPERTY(BlueprintReadOnly, Category = "SimEconomy|Auction")
	int32 SettlementDay = 0;

	/** Lifecycle state. */
	UPROPERTY(BlueprintReadOnly, Category = "SimEconomy|Auction")
	ESimEco_AuctionState State = ESimEco_AuctionState::Active;

	FSimEco_AuctionLot() = default;

	//~ FFastArraySerializerItem replication callbacks (clients only).
	void PostReplicatedAdd(const struct FSimEco_AuctionLotArray& InArraySerializer);
	void PostReplicatedChange(const struct FSimEco_AuctionLotArray& InArraySerializer);
	void PreReplicatedRemove(const struct FSimEco_AuctionLotArray& InArraySerializer);
};

/** Fast-array of public auction lot summaries; only changed lots cross the wire. */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMECONOMY_API FSimEco_AuctionLotArray : public FFastArraySerializer
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SimEconomy|Auction")
	TArray<FSimEco_AuctionLot> Lots;

	UPROPERTY(NotReplicated, Transient)
	TObjectPtr<ASimEco_AuctionReplicationProxy> OwnerProxy = nullptr;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FastArrayDeltaSerialize<FSimEco_AuctionLot, FSimEco_AuctionLotArray>(Lots, DeltaParms, *this);
	}
};

template<>
struct TStructOpsTypeTraits<FSimEco_AuctionLotArray> : public TStructOpsTypeTraitsBase2<FSimEco_AuctionLotArray>
{
	enum { WithNetDeltaSerializer = true };
};
