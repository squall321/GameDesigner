// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "Economy/Seam_PriceQuote.h"
#include "GameplayTagContainer.h"
#include "SimEco_PriceQuoteProvider.generated.h"

class USimEco_MarketSubsystem;
class ASimEco_EconomyReplicationProxy;
class UDP_ServiceLocatorSubsystem;

/** One sampled price point in a commodity's rolling price history (for charts / trend analytics). */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMECONOMY_API FSimEco_PriceSample
{
	GENERATED_BODY()

	/** Sim-clock day number the sample was taken on. */
	UPROPERTY(BlueprintReadOnly, Category = "SimEconomy|Pricing")
	int32 DayNumber = 0;

	/** Clearing price at the sample time. */
	UPROPERTY(BlueprintReadOnly, Category = "SimEconomy|Pricing")
	double Price = 0.0;
};

/** A bounded ring of price samples for one commodity. */
USTRUCT()
struct FSimEco_PriceHistory
{
	GENERATED_BODY()

	/** Most-recent-last samples; trimmed to the provider's MaxHistoryLength. */
	UPROPERTY()
	TArray<FSimEco_PriceSample> Samples;
};

/**
 * World-scoped adapter exposing the demand-formed market price as the cross-module ISeam_PriceQuote.
 *
 * Implements ISeam_PriceQuote and registers itself in the service locator under
 * SimEcoPricingTags::Service_PriceQuote (WeakObserved, since it is a world-lifetime object held by a
 * GameInstance locator). The Progression shop and any UI read live prices through the seam without
 * ever including the SimEconomy market — they resolve the locator key and call GetQuotedPrice.
 *
 * It reads prices ONLY through the public market surface: on the server GetPrice on the market
 * subsystem, on a client the replicated proxy. It also samples a bounded PRICE HISTORY per commodity
 * (one sample per sim day) so trend UI has data without any extra replication — history is local.
 *
 * The reputation→discount fold is NOT done here (this is a faction-neutral quote): the per-buyer
 * multiplier is GetPriceMultiplierForFaction, which resolves ISeam_Reputation off the buyer. Concrete
 * per-merchant spread/haggle live on USimEco_MerchantComponent; this provider is the shared base quote.
 */
UCLASS()
class DESIGNPATTERNSSIMECONOMY_API USimEco_PriceQuoteProvider
	: public UDP_WorldSubsystem
	, public ISeam_PriceQuote
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/** UWorldSubsystem has no HasWorldAuthority of its own — declare our own. */
	bool HasWorldAuthority() const
	{
		const UWorld* W = GetWorld();
		return W && W->GetNetMode() != NM_Client;
	}

	//~ Begin ISeam_PriceQuote
	virtual double GetQuotedPrice_Implementation(FGameplayTag ItemOrCommodityTag) const override;
	virtual float GetPriceMultiplierForFaction_Implementation(FGameplayTag FactionTag, const AActor* Buyer) const override;
	//~ End ISeam_PriceQuote

	//~ Begin UDP_WorldSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_WorldSubsystem

	/**
	 * Append a price sample for CommodityTag on DayNumber (one per day; same-day calls overwrite the
	 * last sample). Server-side; the market clearing or the economy step driver calls this. Trimmed to
	 * MaxHistoryLength. Local-only (history never replicates).
	 */
	void RecordPriceSample(const FGameplayTag& CommodityTag, int32 DayNumber, double Price);

	/** Copy the rolling price history for CommodityTag (most-recent-last). Empty if none recorded. */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Pricing")
	void GetPriceHistory(FGameplayTag CommodityTag, TArray<FSimEco_PriceSample>& OutSamples) const;

	/** Simple linear trend over the recorded history: +1 rising, 0 flat, -1 falling (clamped). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimEconomy|Pricing")
	float GetPriceTrend(FGameplayTag CommodityTag) const;

	/** Maximum samples retained per commodity (designer tunable; a small ring keeps memory bounded). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SimEconomy|Pricing",
		meta = (ClampMin = "2", UIMin = "8", UIMax = "365"))
	int32 MaxHistoryLength = 60;

private:
	/** Per-commodity rolling price history (local-only; never replicated). */
	UPROPERTY(Transient)
	TMap<FGameplayTag, FSimEco_PriceHistory> History;

	/** Resolve the world's market subsystem (server path for live prices). */
	USimEco_MarketSubsystem* ResolveMarket() const;

	/** Resolve the replicated price proxy (client path for live prices). */
	ASimEco_EconomyReplicationProxy* ResolveProxy() const;

	/** Resolve the GameInstance service locator (for self-registration + reputation lookup keys). */
	UDP_ServiceLocatorSubsystem* ResolveLocator() const;

	/** Resolve the current sim day from the shared sim-clock seam (0 if unavailable). */
	int32 ResolveCurrentDay() const;

	/** True once we have registered ourselves as the price-quote service. */
	bool bRegistered = false;
};
