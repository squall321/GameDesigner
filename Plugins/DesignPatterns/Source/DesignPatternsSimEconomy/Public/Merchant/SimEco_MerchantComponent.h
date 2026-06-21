// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "GameplayTagContainer.h"
#include "Economy/SimEco_StepListener.h"
#include "Pricing/SimEco_PriceModifierDef.h"   // ESimEco_TradeSide + USimEco_PriceModifierDef
#include "SimEco_MerchantComponent.generated.h"

class USimEco_MerchantComponent;
class USimEco_PriceModifierDef;
class USimEco_MarketSubsystem;
class USimEco_EconomySubsystem;
class USimEco_StockpileComponent;

/**
 * One commodity/item a merchant deals in, with its restock policy and (optional) limited global stock.
 *
 * Tracked as a fast-array item so a single stock change delta-replicates. A merchant's "specialization"
 * is simply the set of commodities it lists with favourable RestockTarget/markup; off-spec goods can
 * still be authored with poor terms.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMECONOMY_API FSimEco_MerchantStockEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	/** Commodity/item tag this entry tracks (matches a market commodity / inventory item tag). */
	UPROPERTY(BlueprintReadOnly, Category = "SimEconomy|Merchant")
	FGameplayTag CommodityTag;

	/** Current units the merchant has on hand to SELL to players (limited global stock). */
	UPROPERTY(BlueprintReadOnly, Category = "SimEconomy|Merchant")
	int32 Stock = 0;

	/**
	 * Restock pulls Stock back toward this target each restock cycle (a soft cap). 0 = the merchant
	 * never holds this item for direct sale (sell-to-only). Authoring-time per-entry tunable.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "SimEconomy|Merchant")
	int32 RestockTarget = 0;

	/** Units added per restock cycle (the merchant's resupply rate). */
	UPROPERTY(BlueprintReadOnly, Category = "SimEconomy|Merchant")
	int32 RestockPerCycle = 0;

	FSimEco_MerchantStockEntry() = default;

	//~ FFastArraySerializerItem replication callbacks (clients only).
	void PostReplicatedAdd(const struct FSimEco_MerchantStockArray& InArraySerializer);
	void PostReplicatedChange(const struct FSimEco_MerchantStockArray& InArraySerializer);
	void PreReplicatedRemove(const struct FSimEco_MerchantStockArray& InArraySerializer);
};

/** Fast-array of the merchant's per-commodity stock entries; only changed entries cross the wire. */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMECONOMY_API FSimEco_MerchantStockArray : public FFastArraySerializer
{
	GENERATED_BODY()

	/** Replicated stock entries. */
	UPROPERTY(BlueprintReadOnly, Category = "SimEconomy|Merchant")
	TArray<FSimEco_MerchantStockEntry> Entries;

	/** Non-replicated back-pointer to the owning component, for change notifications. */
	UPROPERTY(NotReplicated, Transient)
	TObjectPtr<USimEco_MerchantComponent> OwnerComponent = nullptr;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FastArrayDeltaSerialize<FSimEco_MerchantStockEntry, FSimEco_MerchantStockArray>(Entries, DeltaParms, *this);
	}
};

template<>
struct TStructOpsTypeTraits<FSimEco_MerchantStockArray> : public TStructOpsTypeTraitsBase2<FSimEco_MerchantStockArray>
{
	enum { WithNetDeltaSerializer = true };
};

/** One authored line of a merchant's catalogue: the item plus its restock policy and initial stock. */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMECONOMY_API FSimEco_MerchantCatalogueLine
{
	GENERATED_BODY()

	/** Commodity/item tag. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimEconomy|Merchant")
	FGameplayTag CommodityTag;

	/** Stock the merchant starts with and is restocked toward. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimEconomy|Merchant", meta = (ClampMin = "0"))
	int32 InitialStock = 0;

	/** Soft cap restock pulls toward (0 = the merchant does not sell this, only buys it). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimEconomy|Merchant", meta = (ClampMin = "0"))
	int32 RestockTarget = 0;

	/** Units replenished per restock cycle. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimEconomy|Merchant", meta = (ClampMin = "0"))
	int32 RestockPerCycle = 0;
};

/** Broadcast (server and clients) when the merchant's stock changes, so trade UI refreshes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSimEco_OnMerchantStockChanged, USimEco_MerchantComponent*, Merchant);

/**
 * Server-authoritative MERCHANT: owns a vendor's limited stock, its restock cadence, its pricing rules
 * (spread / scarcity / reputation / haggle via USimEco_PriceModifierDef) and its specialization. Drives
 * the shared market with periodic buy/sell orders so a busy merchant moves the regional price.
 *
 * Lifetime: implements ISimEco_StepListener; registers with the world economy driver on authority in
 * BeginPlay, unregisters in EndPlay. AdvanceEconomyStep restocks toward each line's target and (when
 * configured) places synthetic orders into the market reflecting its trading.
 *
 * Pricing: QuoteUnitPrice folds the market base price through the merchant's price-modifier asset for a
 * given side / buyer / haggle. The TRADE itself (taking money + delivering item) is NOT here — it is on
 * the player-owned USimEco_MerchantTradeComponent, which re-derives the authoritative price server-side
 * (never trusting a client's quoted/haggled number). This component only owns stock + pricing rules.
 *
 * REPLICATION: stock delta-replicates via a fast-array; EVERY mutator guards authority at the TOP.
 */
UCLASS(ClassGroup = (DesignPatternsSimEconomy), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSSIMECONOMY_API USimEco_MerchantComponent
	: public UActorComponent
	, public ISimEco_StepListener
{
	GENERATED_BODY()

public:
	USimEco_MerchantComponent();

	//~ Begin UActorComponent
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent

	//~ Begin ISimEco_StepListener
	/** Server-only. Restock toward targets; (optionally) push synthetic orders into the market. */
	virtual void AdvanceEconomyStep(double StepSeconds, int64 StepIndex, int32 DayNumber) override;
	//~ End ISimEco_StepListener

	// ---- Configuration ----

	/** The merchant's pricing rule set (spread, scarcity, reputation, haggle). May be null (base price). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimEconomy|Merchant")
	TObjectPtr<USimEco_PriceModifierDef> PriceModifier = nullptr;

	/** The faction this merchant prices in (reputation context). Falls back to the modifier's faction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimEconomy|Merchant")
	FGameplayTag MerchantFactionTag;

	/** Authored catalogue: what the merchant deals in, with initial stock + restock policy. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimEconomy|Merchant", meta = (TitleProperty = "CommodityTag"))
	TArray<FSimEco_MerchantCatalogueLine> Catalogue;

	/**
	 * How many economy steps between restock passes. 0 = restock every step. A tunable cadence so a
	 * shop refreshes on a sensible schedule rather than every fixed step.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimEconomy|Merchant", meta = (ClampMin = "0"))
	int32 RestockEveryNSteps = 1;

	// ---- Authoritative API (no-op on clients) ----

	/** (Re)seed replicated stock from Catalogue. AUTHORITY ONLY. Called on BeginPlay; expose for resets. */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Merchant")
	void SeedStockFromCatalogue();

	/** Restock every line toward its target by its per-cycle rate. AUTHORITY ONLY. */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Merchant")
	void RestockNow();

	/**
	 * AUTHORITY ONLY: remove Count units of the merchant's sellable stock for CommodityTag (a player
	 * bought them). Returns the count actually removed (limited by stock). Clients no-op (return 0).
	 */
	int32 ConsumeStockForSale(const FGameplayTag& CommodityTag, int32 Count);

	/**
	 * AUTHORITY ONLY: add Count units to the merchant's stock for CommodityTag (a player sold to it).
	 * Returns the count actually added.
	 */
	int32 AddStockFromPurchase(const FGameplayTag& CommodityTag, int32 Count);

	// ---- Read API (client-safe) ----

	/** True if the merchant lists CommodityTag at all (in its catalogue / stock). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimEconomy|Merchant")
	bool DealsIn(FGameplayTag CommodityTag) const;

	/** Current sellable stock of CommodityTag (0 if not stocked). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimEconomy|Merchant")
	int32 GetStock(FGameplayTag CommodityTag) const;

	/**
	 * Quote the EFFECTIVE per-unit price for CommodityTag on the given side for Buyer with an optional
	 * haggle factor. Reads the market base price and folds it through the price-modifier asset
	 * (reputation resolved off Buyer). Const / client-safe (UI uses it directly). Returns 0 if the
	 * merchant cannot price the item.
	 *
	 * @param CommodityTag     What to price.
	 * @param Side             Player buy (ask) or sell (bid).
	 * @param Buyer            Whom the price is for (reputation source); may be null.
	 * @param HaggleMultiplier Negotiation factor for a buy (clamped to the modifier's floor); 1 = none.
	 */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Merchant")
	int64 QuoteUnitPrice(FGameplayTag CommodityTag, ESimEco_TradeSide Side, const AActor* Buyer,
		float HaggleMultiplier = 1.0f) const;

	/** Currency tag prices are denominated in (designer tunable; defaults from the modifier asset). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimEconomy|Merchant")
	FGameplayTag CurrencyTag;

	/** Fired (server and clients) after stock changes; trade UI binds this. */
	UPROPERTY(BlueprintAssignable, Category = "SimEconomy|Merchant")
	FSimEco_OnMerchantStockChanged OnMerchantStockChanged;

	/** Called by the fast-array entry callbacks on clients to surface a stock change. */
	void HandleReplicatedStockChange();

protected:
	/** True if the owner has network authority. */
	bool HasAuthority() const;

	/** Resolve the world's market subsystem (for base prices + synthetic orders). */
	USimEco_MarketSubsystem* ResolveMarket() const;

	/** Resolve the world's economy driver (to register/unregister as a step listener). */
	USimEco_EconomySubsystem* ResolveEconomy() const;

	/** Find a stock entry index, or INDEX_NONE. */
	int32 FindStockIndex(const FGameplayTag& CommodityTag) const;

	/** Server-side: mark dirty + broadcast OnMerchantStockChanged. */
	void NotifyStockChanged();

private:
	/** Replicated per-commodity stock. */
	UPROPERTY(Replicated)
	FSimEco_MerchantStockArray MerchantStock;

	/** True once stock has been seeded (so BeginPlay does not double-seed after a manual reseed). */
	bool bSeeded = false;

	/** True once registered with the economy driver (idempotent register/unregister). */
	bool bRegisteredWithEconomy = false;

	/** Steps since the last restock pass (for RestockEveryNSteps cadence). */
	int32 StepsSinceRestock = 0;

	/** Resolve the effective faction tag (explicit, else the modifier's). */
	FGameplayTag GetEffectiveFactionTag() const;
};
