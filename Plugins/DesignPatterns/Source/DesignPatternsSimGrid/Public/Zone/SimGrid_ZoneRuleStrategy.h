// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/ScriptInterface.h"
#include "GameplayTagContainer.h"
#include "Grid/Seam_GridCoord.h"
#include "Grid/Seam_TileProviderRead.h"
#include "SimGrid_ZoneRuleStrategy.generated.h"

class ASimGrid_ZoneCarrier;

/**
 * Context handed to a zone-growth rule for one zoned cell on one growth tick. Bundles everything a rule
 * needs to decide a cell's next development without the rule reaching into engine globals.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMGRID_API FSimGrid_ZoneGrowthContext
{
	GENERATED_BODY()

	/** The zoned cell being evaluated. */
	UPROPERTY(BlueprintReadOnly, Category = "SimGrid|Zone")
	FSeam_CellCoord Cell;

	/** The cell's current zone-type tag. */
	UPROPERTY(BlueprintReadOnly, Category = "SimGrid|Zone")
	FGameplayTag ZoneTypeTag;

	/** The cell's current growth level [0,1]. */
	UPROPERTY(BlueprintReadOnly, Category = "SimGrid|Zone")
	float CurrentGrowth = 0.f;

	/** How many sim-days of growth this tick represents (from the clock accumulator). */
	UPROPERTY(BlueprintReadOnly, Category = "SimGrid|Zone")
	float ElapsedDays = 0.f;

	/** The grid (read seam) so a rule can inspect neighbouring tiles/cells. */
	UPROPERTY(BlueprintReadOnly, Category = "SimGrid|Zone")
	TScriptInterface<ISeam_TileProviderRead> Grid;

	/** The zone carrier so a rule can inspect neighbouring zones. */
	UPROPERTY(BlueprintReadOnly, Category = "SimGrid|Zone")
	TObjectPtr<ASimGrid_ZoneCarrier> Carrier = nullptr;
};

/**
 * Strategy base for ZONE GROWTH/EFFECT rules. The zone growth component owns one or more strategies and
 * runs each zoned cell through its matching strategy on every sim-clock growth step. A strategy returns
 * the cell's NEW growth level; the component routes the change through the carrier's authority mutator.
 *
 * Strategies are pure-ish UObjects created via NewObject(Outer) (so they are GC-managed) and carry only
 * data-driven tunables (UPROPERTY EditAnywhere) — no hardcoded growth numbers. Subclass and override
 * EvaluateGrowth_Implementation; the default base is an inert pass-through (growth unchanged).
 */
UCLASS(Abstract, Blueprintable, EditInlineNew, DefaultToInstanced)
class DESIGNPATTERNSSIMGRID_API USimGrid_ZoneRuleStrategy : public UObject
{
	GENERATED_BODY()

public:
	/** The zone-type tag(s) this strategy applies to. A cell matches when its zone matches any of these. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimGrid|Zone")
	FGameplayTagContainer AppliesToZoneTypes;

	/** True if this strategy governs the given zone type. */
	UFUNCTION(BlueprintCallable, Category = "SimGrid|Zone")
	bool AppliesTo(const FGameplayTag& ZoneTypeTag) const
	{
		return AppliesToZoneTypes.IsEmpty() || AppliesToZoneTypes.HasTag(ZoneTypeTag);
	}

	/**
	 * Compute the cell's new growth level [0,1] given the context. Default implementation returns the
	 * current growth unchanged (inert). Override to drive development (e.g. ramp toward 1 when adjacent
	 * to a road, decay when isolated).
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "SimGrid|Zone")
	float EvaluateGrowth(const FSimGrid_ZoneGrowthContext& Context) const;
	virtual float EvaluateGrowth_Implementation(const FSimGrid_ZoneGrowthContext& Context) const;
};

/**
 * Concrete growth rule: a cell's growth ramps toward 1 at GrowthRatePerDay while it has at least
 * RequiredAdjacentMatches neighbouring cells whose zone shares the cell's zone type (clustering pressure),
 * and decays at DecayRatePerDay otherwise. All rates are data-driven tunables. This models the classic
 * city-builder behaviour where well-connected districts densify and isolated ones stagnate.
 */
UCLASS()
class DESIGNPATTERNSSIMGRID_API USimGrid_ClusterGrowthRule : public USimGrid_ZoneRuleStrategy
{
	GENERATED_BODY()

public:
	/** Growth added per sim-day while the clustering condition is met. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimGrid|Zone", meta = (ClampMin = "0.0"))
	float GrowthRatePerDay = 0.25f;

	/** Growth removed per sim-day while the clustering condition is NOT met. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimGrid|Zone", meta = (ClampMin = "0.0"))
	float DecayRatePerDay = 0.1f;

	/** Minimum same-zone 8-neighbours required for the cell to grow rather than decay. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "SimGrid|Zone", meta = (ClampMin = "0"))
	int32 RequiredAdjacentMatches = 2;

	//~ Begin USimGrid_ZoneRuleStrategy
	virtual float EvaluateGrowth_Implementation(const FSimGrid_ZoneGrowthContext& Context) const override;
	//~ End USimGrid_ZoneRuleStrategy
};
