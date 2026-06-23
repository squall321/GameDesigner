// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UObject/ScriptInterface.h"
#include "GameplayTagContainer.h"
#include "Clock/Seam_SimClock.h"
#include "Grid/Seam_TileProviderRead.h"
#include "SimGrid_ZoneGrowthComponent.generated.h"

class ASimGrid_ZoneCarrier;
class USimGrid_ZoneRuleStrategy;

/**
 * Authority-side driver that ADVANCES zone development over simulation time. It ticks a fixed-step
 * accumulator from the shared simulation clock (ISeam_SimClock), and on each step runs every zoned cell
 * through its matching USimGrid_ZoneRuleStrategy, writing the resulting growth back through the zone
 * carrier's authority mutator (SetZoneGrowth) — never touching replicated state directly.
 *
 * CLOCK INTEGRATION mirrors the economy pattern: an optional ClockOverride is honoured, otherwise the
 * clock is resolved from the service locator; the accumulator advances by RealDelta * GetTimeScale() and
 * is skipped entirely while IsPaused(). Growth is expressed per sim-DAY (rules read ElapsedDays), with
 * SecondsPerSimDay converting accumulated sim-seconds to days — no bare seconds-vs-days confusion.
 *
 * AUTHORITY ONLY: the tick early-returns on clients (which receive growth via replication), and every
 * write is guarded by the carrier's own HasAuthority() check. All tunables are EditAnywhere; nothing is
 * hardcoded.
 */
UCLASS(ClassGroup = (SimGrid), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSSIMGRID_API USimGrid_ZoneGrowthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USimGrid_ZoneGrowthComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent

	/** Optionally inject the clock directly (e.g. from the game mode) instead of resolving via the locator. */
	UFUNCTION(BlueprintCallable, Category = "SimGrid|Zone")
	void SetClockOverride(const TScriptInterface<ISeam_SimClock>& InClock) { ClockOverride = InClock; }

	/** Run one growth step immediately for the given elapsed sim-days (for testing / scripted advance). */
	UFUNCTION(BlueprintCallable, Category = "SimGrid|Zone")
	void StepGrowth(float ElapsedDays);

protected:
	/** Real seconds of accumulated SIM time (RealDelta * TimeScale) per growth step. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimGrid|Zone", meta = (ClampMin = "0.01"))
	float StepIntervalSimSeconds = 1.f;

	/** Sim-seconds that constitute one sim-DAY, converting the accumulator to the day units rules use. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimGrid|Zone", meta = (ClampMin = "0.01"))
	float SecondsPerSimDay = 60.f;

	/** Service-locator key the clock is published under (defaults to the shared DP.Service.SimClock). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimGrid|Zone")
	FGameplayTag ClockServiceTag;

	/**
	 * Growth/effect strategies applied to zoned cells. Instanced subobjects so each carries its own
	 * tunables; created/owned here (UPROPERTY) so they are GC-rooted. A cell is run through the first
	 * strategy that AppliesTo its zone type.
	 */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "SimGrid|Zone")
	TArray<TObjectPtr<USimGrid_ZoneRuleStrategy>> Strategies;

private:
	/** Clock injected via SetClockOverride; takes precedence over locator resolution. */
	UPROPERTY(Transient)
	TScriptInterface<ISeam_SimClock> ClockOverride;

	/** Accumulated SIM seconds toward the next growth step. */
	float SimSecondAccumulator = 0.f;

	/**
	 * One-frame weak cache of the resolved tile provider; reset automatically when the provider is
	 * GC'd (level unload). ResolveGrid() repopulates it on the next call.
	 */
	mutable TWeakObjectPtr<UObject> CachedGridObject;

	/** Resolve the active clock: the override if set, else the locator-published clock (or empty). */
	TScriptInterface<ISeam_SimClock> ResolveClock() const;

	/** Resolve the grid read seam from the locator (for rules that inspect neighbouring tiles). */
	TScriptInterface<ISeam_TileProviderRead> ResolveGrid() const;

	/** Run a single growth step across every zoned cell for ElapsedDays of development. */
	void RunGrowthStep(float ElapsedDays);

	/** Find the strategy that governs ZoneType, or null. */
	USimGrid_ZoneRuleStrategy* FindStrategy(const FGameplayTag& ZoneType) const;
};
