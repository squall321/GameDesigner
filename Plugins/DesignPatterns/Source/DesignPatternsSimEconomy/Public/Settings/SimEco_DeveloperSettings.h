// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "SimEco_DeveloperSettings.generated.h"

class USimEco_MarketSettings;

/**
 * Project-wide tunables for the simulation economy module.
 *
 * These are config-backed designer/engineer knobs (NOT per-actor gameplay numbers — those live
 * on data assets and components). Exposed in Project Settings under "Plugins > DesignPatterns SimEconomy".
 *
 * The epsilons exist because economy quantities and prices are doubles: tiny floating residues
 * from repeated reserve/commit/produce cycles must be clamped to zero (QuantityEpsilon) and
 * sub-perceptible price wobble must not generate replication/bus traffic (PriceReplicationEpsilon).
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "DesignPatterns SimEconomy"))
class DESIGNPATTERNSSIMECONOMY_API USimEco_DeveloperSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	USimEco_DeveloperSettings();

	/** Convenience accessor for the CDO-backed config settings object (never null). */
	static const USimEco_DeveloperSettings* Get();

	//~ Begin UDeveloperSettings
	virtual FName GetCategoryName() const override { return FName(TEXT("Plugins")); }
	//~ End UDeveloperSettings

	/**
	 * Default market-rules asset applied to a region/market that does not specify its own.
	 * Soft so the settings object never force-loads economy content at startup.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "SimEconomy|Market")
	TSoftObjectPtr<USimEco_MarketSettings> DefaultMarketSettings;

	/**
	 * Fixed simulation step, in simulation seconds, used by producer/consumer accumulators when a
	 * process does not override it. Smaller = finer progress granularity at more CPU cost.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "SimEconomy|Step",
		meta = (ClampMin = "0.01", UIMin = "0.05", UIMax = "10.0", ForceUnits = "s"))
	double DefaultFixedStepSeconds = 1.0;

	/**
	 * Quantities whose magnitude is below this are treated as exactly zero. Clamps floating-point
	 * residue from repeated reserve/commit/release/produce cycles so stockpiles settle cleanly.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "SimEconomy|Precision",
		meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "0.01"))
	double QuantityEpsilon = 1.0e-4;

	/**
	 * Minimum absolute price change that is allowed to dirty replicated price state / raise a
	 * PriceChanged bus event. Suppresses sub-perceptible wobble and the bandwidth it would cost.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "SimEconomy|Precision",
		meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "1.0"))
	double PriceReplicationEpsilon = 1.0e-2;

	/**
	 * When true, producer/consumer components that find no explicitly assigned clock will try to
	 * resolve the shared sim-clock seam (typically the Survival day-night clock) from the service
	 * locator so the economy advances on the same simulation timeline as the rest of the world.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "SimEconomy|Clock")
	bool bAutoBindSurvivalClock = true;
};
