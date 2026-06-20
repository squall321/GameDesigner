// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "GameplayTagContainer.h"
#include "Net/Seam_NetValue.h"
#include "SimEco_EconomyReplicationProxy.generated.h"

class ASimEco_EconomyReplicationProxy;

/**
 * One delta-replicated price entry: a commodity tag and its current clearing price carried as an
 * FSeam_NetValue (the only arbitrary-typed value allowed across the wire). Lives inside a fast-array
 * so individual commodity price changes delta-replicate rather than resending the whole summary.
 */
USTRUCT()
struct DESIGNPATTERNSSIMECONOMY_API FSimEco_PriceEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	/** Commodity this price applies to. */
	UPROPERTY()
	FGameplayTag CommodityTag;

	/** Clearing price (FSeam_NetValue::MakeFloat). Compact-serialized by FSeam_NetValue::NetSerialize. */
	UPROPERTY()
	FSeam_NetValue Price;

	//~ FFastArraySerializerItem replication callbacks (clients only).
	void PostReplicatedAdd(const struct FSimEco_PriceArray& InArraySerializer);
	void PostReplicatedChange(const struct FSimEco_PriceArray& InArraySerializer);
	void PreReplicatedRemove(const struct FSimEco_PriceArray& InArraySerializer);
};

/**
 * Fast-array serializer holding the replicated price summary. NetDeltaSerialize forwards to
 * FastArrayDeltaSerialize so only changed entries cross the wire. The owner back-pointer is set on
 * both server and client so entry callbacks can notify the proxy actor.
 */
USTRUCT()
struct DESIGNPATTERNSSIMECONOMY_API FSimEco_PriceArray : public FFastArraySerializer
{
	GENERATED_BODY()

	/** Replicated price entries, one per commodity that has crossed the replication epsilon at least once. */
	UPROPERTY()
	TArray<FSimEco_PriceEntry> Entries;

	/** Non-replicated back-pointer to the owning proxy, for change notifications. */
	UPROPERTY(NotReplicated, Transient)
	TObjectPtr<ASimEco_EconomyReplicationProxy> OwnerProxy = nullptr;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FastArrayDeltaSerialize<FSimEco_PriceEntry, FSimEco_PriceArray>(Entries, DeltaParms, *this);
	}
};

/** Enables NetDeltaSerialize for the price array. */
template<>
struct TStructOpsTypeTraits<FSimEco_PriceArray> : public TStructOpsTypeTraitsBase2<FSimEco_PriceArray>
{
	enum { WithNetDeltaSerializer = true };
};

/** Broadcast (server and clients) after the replicated price summary changes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSimEco_OnPriceSummaryChanged, ASimEco_EconomyReplicationProxy*, Proxy);

/**
 * Replicated carrier for the market's price summary.
 *
 * Subsystems are never replicated, so the authoritative market subsystem pushes a delta-throttled
 * snapshot of clearing prices into THIS AInfo (one per world, spawned by the market on authority).
 * Clients receive the fast-array delta and re-broadcast it locally; the market subsystem on a client
 * reads prices from here. Only price changes that exceed PriceReplicationEpsilon are written, so
 * sub-perceptible wobble costs no bandwidth.
 *
 * SyncFromServer is the single authority-only entry point the market calls each clearing.
 */
UCLASS(NotPlaceable, Transient)
class DESIGNPATTERNSSIMECONOMY_API ASimEco_EconomyReplicationProxy : public AInfo
{
	GENERATED_BODY()

public:
	ASimEco_EconomyReplicationProxy();

	//~ Begin AActor
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PostInitProperties() override;
	//~ End AActor

	/**
	 * AUTHORITY ONLY. Upsert the price for CommodityTag if it has moved by at least Epsilon since the
	 * last replicated value (or is new). Marks the entry dirty so it delta-replicates. Returns true
	 * when a change was written. Early-returns false on clients.
	 *
	 * @param CommodityTag  Commodity whose price to publish.
	 * @param NewPrice      New clearing price.
	 * @param Epsilon       Minimum absolute change required to write (the PriceReplicationEpsilon).
	 */
	bool SyncFromServer(const FGameplayTag& CommodityTag, double NewPrice, double Epsilon);

	/** Read the last-replicated price for CommodityTag (server: authoritative copy; client: replicated). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimEconomy|Market")
	double GetReplicatedPrice(FGameplayTag CommodityTag) const;

	/** True if a price entry exists for CommodityTag. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimEconomy|Market")
	bool HasPrice(FGameplayTag CommodityTag) const;

	/** Copy every replicated (commodity, price) pair out for UI/debug. */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Market")
	void GetAllPrices(TArray<FGameplayTag>& OutCommodities, TArray<double>& OutPrices) const;

	/** Fired (server and clients) after the price summary changes. */
	UPROPERTY(BlueprintAssignable, Category = "SimEconomy|Market")
	FSimEco_OnPriceSummaryChanged OnPriceSummaryChanged;

	/** Called by the fast-array entry callbacks on clients to surface a change. */
	void HandleReplicatedChange();

private:
	/** Delta-replicated price summary. */
	UPROPERTY(Replicated)
	FSimEco_PriceArray Prices;

	/** Locate an entry by commodity (mutable). */
	FSimEco_PriceEntry* FindEntry(const FGameplayTag& CommodityTag);
	const FSimEco_PriceEntry* FindEntry(const FGameplayTag& CommodityTag) const;
};
