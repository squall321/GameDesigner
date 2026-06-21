// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "SimEco_EconomicEventDef.generated.h"

/** Whether an economic event pushes the price of its commodities UP (shortage) or DOWN (boom/glut). */
UENUM(BlueprintType)
enum class ESimEco_EventKind : uint8
{
	/** Scarcity: inject synthetic DEMAND, driving prices up. */
	Shortage,
	/** Glut/boom: inject synthetic SUPPLY, driving prices down. */
	Boom
};

/** One commodity an event affects, with the per-step synthetic order quantity it injects. */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMECONOMY_API FSimEco_EventCommodity
{
	GENERATED_BODY()

	/** Affected commodity (a market commodity tag). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimEconomy|Events", meta = (Categories = "SimEco.Commodity"))
	FGameplayTag CommodityTag;

	/**
	 * Synthetic order quantity injected into the market PER economy step for the event's duration. For a
	 * Shortage this is added as buy-side demand; for a Boom as sell-side supply. A tunable, not a magic
	 * number. Larger = a sharper price move.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimEconomy|Events", meta = (ClampMin = "0.0"))
	double SyntheticQuantityPerStep = 0.0;
};

/**
 * Tag-keyed ECONOMIC-EVENT data asset (a shortage or a boom). Drives synthetic supply/demand into the
 * SHARED market for a duration, moving prices without bespoke per-commodity code. Every value is a
 * designer tunable.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSSIMECONOMY_API USimEco_EconomicEventDef : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	USimEco_EconomicEventDef();

	//~ Begin UDP_DataAsset
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset

	/** Whether this event raises (Shortage) or lowers (Boom) prices. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimEconomy|Events")
	ESimEco_EventKind Kind = ESimEco_EventKind::Shortage;

	/** Human-facing classification tag (e.g. SimEco.Event.Shortage.Famine) for UI / notifications. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimEconomy|Events")
	FGameplayTag EventTag;

	/** Commodities this event affects, with their per-step synthetic order quantity. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimEconomy|Events", meta = (TitleProperty = "CommodityTag"))
	TArray<FSimEco_EventCommodity> Commodities;

	/** How many economy steps the event lasts once triggered. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimEconomy|Events", meta = (ClampMin = "1"))
	int32 DurationSteps = 10;
};
