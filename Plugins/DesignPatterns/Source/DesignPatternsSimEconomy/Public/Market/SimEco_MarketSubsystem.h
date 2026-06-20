// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "Market/SimEco_Market.h"
#include "Persist/Seam_Persistable.h"
#include "GameplayTagContainer.h"

class USimEco_MarketSettings;
class ASimEco_EconomyReplicationProxy;

// FInstancedStruct lives in StructUtils on UE 5.3/5.4, merged into CoreUObject in 5.5.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

// .generated.h MUST be the last include (UnrealHeaderTool requirement).
#include "SimEco_MarketSubsystem.generated.h"

/**
 * Message-bus payload broadcast on SimEcoNativeTags::Bus_PriceChanged when a commodity's clearing
 * price moves beyond the replication epsilon. Carried inside an FInstancedStruct over the local bus.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMECONOMY_API FSimEco_PriceChangedMsg
{
	GENERATED_BODY()

	/** Commodity whose price changed. */
	UPROPERTY(BlueprintReadOnly, Category = "SimEconomy|Market")
	FGameplayTag CommodityTag;

	/** Previous clearing price. */
	UPROPERTY(BlueprintReadOnly, Category = "SimEconomy|Market")
	double OldPrice = 0.0;

	/** New clearing price. */
	UPROPERTY(BlueprintReadOnly, Category = "SimEconomy|Market")
	double NewPrice = 0.0;
};

/** One persisted commodity price (commodity tag + clearing price) inside a market save record. */
USTRUCT()
struct DESIGNPATTERNSSIMECONOMY_API FSimEco_PersistedPrice
{
	GENERATED_BODY()

	UPROPERTY(SaveGame)
	FGameplayTag CommodityTag;

	UPROPERTY(SaveGame)
	double Price = 0.0;
};

/**
 * Durable record of a market's price book. Captured/restored via ISeam_Persistable so clearing
 * prices survive a save/load. Supply/demand are transient (rebuilt from live orders) and not saved.
 */
USTRUCT()
struct DESIGNPATTERNSSIMECONOMY_API FSimEco_MarketSaveRecord
{
	GENERATED_BODY()

	UPROPERTY(SaveGame)
	TArray<FSimEco_PersistedPrice> Prices;
};

/**
 * Live, server-side book for one commodity: the price plus the supply/demand accumulated from
 * outstanding orders since the last clearing. Server-only state — never replicated directly (the
 * replication proxy carries the throttled price summary instead).
 */
USTRUCT()
struct FSimEco_CommodityBook
{
	GENERATED_BODY()

	/** Current clearing price. */
	double Price = 0.0;

	/** Aggregate sell-side quantity queued for the next clearing. */
	double Supply = 0.0;

	/** Aggregate buy-side quantity queued for the next clearing. */
	double Demand = 0.0;
};

/**
 * World-scoped, server-authoritative price-forming market.
 *
 * Implements ISimEco_Market. Holds a per-commodity order book (server-only). PlaceOrder accumulates
 * supply/demand (authority API, NOT an RPC — client intent arrives via a player-owned component's
 * Server_PlaceOrder). ClearMarket, called by the economy fixed-step driver, forms a new price from
 * each commodity's supply/demand imbalance against its USimEco_MarketSettings rule, clamps it to the
 * rule band, drains the book, and pushes any change (beyond PriceReplicationEpsilon) into the
 * replicated price proxy and onto the message bus.
 *
 * Persistable: the price book is captured/restored through ISeam_Persistable so prices survive saves.
 * Subsystems are NEVER replicated — clients read GetPrice from the replicated proxy.
 */
UCLASS()
class DESIGNPATTERNSSIMECONOMY_API USimEco_MarketSubsystem
	: public UDP_WorldSubsystem
	, public ISimEco_Market
	, public ISeam_Persistable
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/**
	 * UWorldSubsystem has no HasWorldAuthority of its own — declare our own. True on the server
	 * (listen/dedicated/standalone), false on a net client. Every mutator guards on this.
	 */
	bool HasWorldAuthority() const
	{
		const UWorld* W = GetWorld();
		return W && W->GetNetMode() != NM_Client;
	}

	//~ Begin ISimEco_Market
	virtual double GetPrice_Implementation(FGameplayTag CommodityTag) const override;
	virtual double GetSupply_Implementation(FGameplayTag CommodityTag) const override;
	virtual double GetDemand_Implementation(FGameplayTag CommodityTag) const override;
	virtual FSimEco_OrderReceipt PlaceOrder_Implementation(const FSimEco_Order& Order) override;
	virtual void ClearMarket_Implementation() override;
	//~ End ISimEco_Market

	//~ Begin ISeam_Persistable
	virtual void CaptureState_Implementation(FInstancedStruct& Out) const override;
	virtual void RestoreState_Implementation(const FInstancedStruct& In) override;
	virtual FGameplayTag GetPersistenceKind_Implementation() const override;
	//~ End ISeam_Persistable

	//~ Begin UDP_WorldSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_WorldSubsystem

	/**
	 * Assign the market-rules asset (loaded synchronously by the caller). When unset, the market
	 * falls back to USimEco_DeveloperSettings::DefaultMarketSettings. AUTHORITY ONLY.
	 */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Market")
	void SetMarketSettings(USimEco_MarketSettings* InSettings);

	/** The replicated price proxy this market pushes prices into (server spawns it lazily). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimEconomy|Market")
	ASimEco_EconomyReplicationProxy* GetReplicationProxy() const { return ReplicationProxy.Get(); }

private:
	/** Per-commodity server-side order book, keyed by commodity tag. Server-only. */
	UPROPERTY(Transient)
	TMap<FGameplayTag, FSimEco_CommodityBook> Books;

	/** Active market-rules asset; resolved from settings on first use if null. */
	UPROPERTY(Transient)
	TObjectPtr<USimEco_MarketSettings> Settings = nullptr;

	/** Replicated price carrier; spawned on authority during Initialize. */
	UPROPERTY(Transient)
	TWeakObjectPtr<ASimEco_EconomyReplicationProxy> ReplicationProxy;

	/** True once we have resolved (or attempted to resolve) the settings asset. */
	bool bSettingsResolved = false;

	/** Resolve Settings from the developer settings' default asset if not explicitly set. */
	void EnsureSettings();

	/** Get-or-create the book for a commodity (server-only mutation path). */
	FSimEco_CommodityBook& FindOrAddBook(const FGameplayTag& CommodityTag);

	/** Spawn the replication proxy on authority (idempotent). */
	void EnsureReplicationProxy();

	/** Read the configured PriceReplicationEpsilon from developer settings. */
	double GetPriceEpsilon() const;
};
