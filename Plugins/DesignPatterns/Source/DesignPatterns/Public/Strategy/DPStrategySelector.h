// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Strategy/DPStrategy.h"
#include "DPStrategySelector.generated.h"

/**
 * Chooses one strategy from a candidate list for a given context.
 *
 * EditInlineNew so a selector (and its inline strategies) can be authored directly on a state
 * or anywhere a designer wants policy-driven behaviour. The base Select returns the first
 * applicable strategy; subclasses implement scoring or priority policies.
 */
UCLASS(EditInlineNew, DefaultToInstanced, Blueprintable, CollapseCategories)
class DESIGNPATTERNS_API UDP_StrategySelector : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Pick the strategy to run for Context.
	 * Base implementation returns the first non-null candidate whose ScoreFor > 0.
	 * @return the chosen strategy, or null if none is applicable.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Strategy")
	virtual UDP_Strategy* Select(const FDP_StrategyContext& Context) const;

	/** Convenience: Select then Execute the winner. @return the executed strategy (or null). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Strategy")
	UDP_Strategy* SelectAndExecute(const FDP_StrategyContext& Context);

	/** Candidate strategies, authored inline. Order is meaningful for priority-based selectors. */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "DesignPatterns|Strategy")
	TArray<TObjectPtr<UDP_Strategy>> Strategies;
};

/**
 * Selector that runs the strategy with the single highest ScoreFor value.
 * Ties resolve to the earliest entry in the array. Strategies scoring <= 0 are ignored.
 */
UCLASS(EditInlineNew, DefaultToInstanced)
class DESIGNPATTERNS_API UDP_HighestScoreSelector : public UDP_StrategySelector
{
	GENERATED_BODY()

public:
	virtual UDP_Strategy* Select(const FDP_StrategyContext& Context) const override;

	/** Strategies must score strictly above this threshold to be eligible. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Strategy")
	float ScoreThreshold = 0.f;
};

/**
 * Selector that walks the array in order and returns the first strategy that is "applicable"
 * (ScoreFor > 0). Array order IS the priority; cheapest deterministic policy.
 */
UCLASS(EditInlineNew, DefaultToInstanced)
class DESIGNPATTERNS_API UDP_PrioritySelector : public UDP_StrategySelector
{
	GENERATED_BODY()

public:
	virtual UDP_Strategy* Select(const FDP_StrategyContext& Context) const override;
};
