// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "SimEco_ProcessDef.generated.h"

/**
 * One side of a recipe: a commodity and the quantity consumed/produced per cycle.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMECONOMY_API FSimEco_CommodityAmount
{
	GENERATED_BODY()

	/** Identity tag of the commodity (matches USimEco_CommodityDef::DataTag). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimEconomy|Process")
	FGameplayTag Commodity;

	/** Units consumed (input) or produced (output) per completed cycle. Must be > 0 to be meaningful. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimEconomy|Process",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	double Quantity = 0.0;

	FSimEco_CommodityAmount() = default;
	FSimEco_CommodityAmount(const FGameplayTag& InCommodity, double InQuantity)
		: Commodity(InCommodity), Quantity(InQuantity) {}

	bool IsValidAmount() const { return Commodity.IsValid() && Quantity > 0.0; }
};

/**
 * Tag-keyed production/consumption recipe: convert Inputs into Outputs over CycleSeconds at a
 * facility of RequiredFacilityTag.
 *
 * A pure-consumption process has empty Outputs (population upkeep, fuel burning); a pure-gathering
 * process has empty Inputs (mining a deposit). Producer/consumer components reference a process by
 * its DataTag and resolve this definition from the core registry, so the recipe can be retuned in
 * content without touching saved/replicated state.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSSIMECONOMY_API USimEco_ProcessDef : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	USimEco_ProcessDef();

	//~ Begin UDP_DataAsset
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset

	/** Commodities consumed (and reserved) per cycle. Empty for a pure-gathering process. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimEconomy|Process", meta = (TitleProperty = "Commodity"))
	TArray<FSimEco_CommodityAmount> Inputs;

	/** Commodities produced per completed cycle. Empty for a pure-consumption process. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimEconomy|Process", meta = (TitleProperty = "Commodity"))
	TArray<FSimEco_CommodityAmount> Outputs;

	/**
	 * Simulation-seconds for one full cycle. Producer/consumer accumulators advance in scaled sim
	 * time, so this is independent of frame rate and honors the sim clock's time scale / pause.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimEconomy|Process",
		meta = (ClampMin = "0.01", UIMin = "0.1", ForceUnits = "s"))
	double CycleSeconds = 5.0;

	/**
	 * Facility-type tag the running site must satisfy (child of DP.SimEco.Facility). Invalid means
	 * the process needs no special facility. The component checks its owner's facility tag against this.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimEconomy|Process")
	FGameplayTag RequiredFacilityTag;

	/** True if this recipe yields any output (a producer recipe); false = pure consumption. */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Process")
	bool HasOutputs() const { return Outputs.Num() > 0; }

	/** True if this recipe draws any input. */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Process")
	bool HasInputs() const { return Inputs.Num() > 0; }

#if WITH_EDITOR
	//~ Begin UObject
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
	//~ End UObject
#endif
};
