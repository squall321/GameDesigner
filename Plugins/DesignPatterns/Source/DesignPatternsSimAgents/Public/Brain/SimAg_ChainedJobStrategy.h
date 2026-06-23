// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Brain/SimAg_JobStrategy.h"
#include "SimAg_ChainedJobStrategy.generated.h"

/**
 * ADDITIVE subclass of the shipped USimAg_JobStrategy that drives a multi-step JOB CHAIN
 * (USimAg_JobChainAsset, resolved by ChainTag through the data registry). It picks the next eligible step
 * whose prerequisites are met, honours the step's RequiredSkill against the agent's USimAg_SkillComponent
 * before committing, and only then claims/posts. When ChainTag is unset it falls back to the base
 * single-job behaviour verbatim — so it is a strict, drop-in superset of the base strategy.
 *
 * Completed step-kinds are tracked on the blackboard as per-kind BOOL keys (the blackboard has no tag /
 * container type): one bool named "<CompletedKindsPrefix>.<StepKind>" per finished step. The strategy
 * sets a step's bool when it completes that step's job, and reads them all back to rebuild the completed
 * set each pass — so the chain advances without this strategy holding hidden member state.
 */
UCLASS(EditInlineNew, DefaultToInstanced, meta = (DisplayName = "SimAg Chained Job Strategy"))
class DESIGNPATTERNSSIMAGENTS_API USimAg_ChainedJobStrategy : public USimAg_JobStrategy
{
	GENERATED_BODY()

public:
	USimAg_ChainedJobStrategy();

	//~ Begin UDP_Strategy
	virtual float ScoreFor_Implementation(const FDP_StrategyContext& Context) const override;
	virtual void Execute_Implementation(const FDP_StrategyContext& Context) override;
	//~ End UDP_Strategy

	/** The job chain to follow (a USimAg_JobChainAsset DataTag). Empty = behave as the base job strategy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Brain")
	FGameplayTag ChainTag;

	/**
	 * Prefix for the per-step-kind completion bool keys on the blackboard. A step kind "X.Y.Z" completed
	 * sets the bool "<prefix>.X.Y.Z". Designer-overridable to share a convention with other systems.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Brain")
	FName CompletedKindsPrefix = TEXT("ChainDone");
};
