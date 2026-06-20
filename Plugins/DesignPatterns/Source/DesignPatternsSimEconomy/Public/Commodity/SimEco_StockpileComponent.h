// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "GameplayTagContainer.h"
#include "SimEco_StockpileComponent.generated.h"

class USimEco_StockpileComponent;

/**
 * One commodity's holding in a stockpile.
 *
 * Tracked as a fast-array item so individual commodity quantity changes delta-replicate instead of
 * resending the whole stockpile. Quantities are doubles to support divisible commodities; the
 * indivisible case is enforced by the commodity definition's Quantize.
 *
 * Reserved is the portion currently earmarked by a producer/consumer/order but not yet committed,
 * so two systems can't both spend the same units. Available = Quantity - Reserved.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMECONOMY_API FSimEco_StockEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	/** Identity tag of the commodity (matches USimEco_CommodityDef::DataTag). */
	UPROPERTY(BlueprintReadOnly, Category = "SimEconomy|Stockpile")
	FGameplayTag Commodity;

	/** Total units physically present (reserved + free). Always >= 0. */
	UPROPERTY(BlueprintReadOnly, Category = "SimEconomy|Stockpile")
	double Quantity = 0.0;

	/** Units earmarked by a pending reservation, not yet committed. 0 <= Reserved <= Quantity. */
	UPROPERTY(BlueprintReadOnly, Category = "SimEconomy|Stockpile")
	double Reserved = 0.0;

	FSimEco_StockEntry() = default;
	explicit FSimEco_StockEntry(const FGameplayTag& InCommodity) : Commodity(InCommodity) {}

	/** Free (unreserved) units. */
	double GetAvailable() const { return FMath::Max(0.0, Quantity - Reserved); }

	//~ FFastArraySerializerItem replication callbacks (client side only).
	void PreReplicatedRemove(const struct FSimEco_StockArray& InArraySerializer);
	void PostReplicatedAdd(const struct FSimEco_StockArray& InArraySerializer);
	void PostReplicatedChange(const struct FSimEco_StockArray& InArraySerializer);
};

/**
 * Fast-array serializer holding the stockpile's per-commodity entries. NetDeltaSerialize forwards
 * to FastArrayDeltaSerialize so only changed commodities cross the wire.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMECONOMY_API FSimEco_StockArray : public FFastArraySerializer
{
	GENERATED_BODY()

	/** Replicated commodity entries. */
	UPROPERTY(BlueprintReadOnly, Category = "SimEconomy|Stockpile")
	TArray<FSimEco_StockEntry> Entries;

	/** Non-replicated back-pointer to the owning component, for change notifications. */
	UPROPERTY(NotReplicated, Transient)
	TObjectPtr<USimEco_StockpileComponent> OwnerComponent = nullptr;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FastArrayDeltaSerialize<FSimEco_StockEntry, FSimEco_StockArray>(Entries, DeltaParms, *this);
	}
};

/** Enables NetDeltaSerialize for the stock array. */
template<>
struct TStructOpsTypeTraits<FSimEco_StockArray> : public TStructOpsTypeTraitsBase2<FSimEco_StockArray>
{
	enum { WithNetDeltaSerializer = true };
};

/** Broadcast (server and clients) whenever stockpile contents change. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSimEco_OnStockChanged, USimEco_StockpileComponent*, Stockpile);

/**
 * Server-authoritative, replicated commodity container with a reservation system.
 *
 * Contents delta-replicate via a FFastArraySerializer. EVERY mutator (Add/Remove/Reserve/
 * CommitReserved/ReleaseReserved) guards authority at the TOP and early-returns on clients, so
 * clients only observe state through OnStockChanged. Quantities below the configured QuantityEpsilon
 * (USimEco_DeveloperSettings) are clamped to zero so floating residue never accumulates.
 *
 * Reservations let a producer/consumer/market lock the inputs it intends to spend before a cycle
 * completes; CommitReserved finalizes the spend, ReleaseReserved hands them back on cancel.
 */
UCLASS(ClassGroup = (DesignPatternsSimEconomy), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSSIMECONOMY_API USimEco_StockpileComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USimEco_StockpileComponent();

	//~ Begin UActorComponent
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent

	// ---- Authoritative mutators (no-op on clients) ----

	/**
	 * Add Quantity units of Commodity (quantized per its definition). AUTHORITY ONLY.
	 * Returns the amount actually added (0 on clients / invalid input).
	 */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Stockpile")
	double Add(FGameplayTag Commodity, double Quantity);

	/**
	 * Remove up to Quantity units of free (unreserved) Commodity. AUTHORITY ONLY.
	 * Returns the amount actually removed. Reserved units are protected and never removed here.
	 */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Stockpile")
	double Remove(FGameplayTag Commodity, double Quantity);

	/**
	 * Earmark up to Quantity free units of Commodity for later commit. AUTHORITY ONLY.
	 * Returns the amount actually reserved (limited by what is available).
	 */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Stockpile")
	double Reserve(FGameplayTag Commodity, double Quantity);

	/**
	 * Finalize a prior reservation: consume up to Quantity reserved units, lowering both Reserved
	 * and Quantity. AUTHORITY ONLY. Returns the amount actually committed.
	 */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Stockpile")
	double CommitReserved(FGameplayTag Commodity, double Quantity);

	/**
	 * Hand reserved units back to the free pool without consuming them (cancel). AUTHORITY ONLY.
	 * Returns the amount actually released.
	 */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Stockpile")
	double ReleaseReserved(FGameplayTag Commodity, double Quantity);

	// ---- Read API (client-safe) ----

	/** Total units (reserved + free) of Commodity. */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Stockpile")
	double GetQuantity(FGameplayTag Commodity) const;

	/** Reserved (earmarked) units of Commodity. */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Stockpile")
	double GetReserved(FGameplayTag Commodity) const;

	/** Free (Quantity - Reserved) units of Commodity. */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Stockpile")
	double GetAvailable(FGameplayTag Commodity) const;

	/** True if at least Quantity free units of Commodity are present. */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Stockpile")
	bool HasAvailable(FGameplayTag Commodity, double Quantity) const { return GetAvailable(Commodity) >= (Quantity - GetQuantityEpsilon()); }

	/** Snapshot of all entries (read-only copy; safe on clients). */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Stockpile")
	TArray<FSimEco_StockEntry> GetAllEntries() const { return Stock.Entries; }

	/**
	 * Designer hook fired after the stockpile changes (server and clients). Default implementation
	 * broadcasts OnStockChanged; override to extend.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "SimEconomy|Stockpile")
	void NotifyStockChanged();
	virtual void NotifyStockChanged_Implementation();

	/** Broadcast whenever stockpile contents change (after replication on clients). */
	UPROPERTY(BlueprintAssignable, Category = "SimEconomy|Stockpile")
	FSimEco_OnStockChanged OnStockChanged;

	/** Called by the fast-array entry callbacks on clients to surface a content change. */
	void HandleReplicatedChange();

protected:
	//~ Begin UActorComponent
	virtual void InitializeComponent() override;
	//~ End UActorComponent

	/** True if this component's owner has network authority. */
	bool HasAuthority() const;

	/** Project-configured quantity epsilon (residue clamp); falls back to a small default. */
	double GetQuantityEpsilon() const;

	/** Quantize a quantity according to the commodity definition's divisibility (from the registry). */
	double QuantizeForCommodity(const FGameplayTag& Commodity, double RawQuantity) const;

	/** Find an entry index by commodity tag, or INDEX_NONE. */
	int32 FindEntryIndex(const FGameplayTag& Commodity) const;

	/** Find or create the entry for Commodity (server side). Returns a stable reference. */
	FSimEco_StockEntry& FindOrAddEntry(const FGameplayTag& Commodity);

	/**
	 * Drop an entry that has become fully empty (Quantity and Reserved both ~0), keeping the array
	 * compact. Server side. Returns true if the entry was removed.
	 */
	bool PruneIfEmpty(int32 EntryIndex);

private:
	/** Replicated commodity holdings. */
	UPROPERTY(Replicated)
	FSimEco_StockArray Stock;
};
