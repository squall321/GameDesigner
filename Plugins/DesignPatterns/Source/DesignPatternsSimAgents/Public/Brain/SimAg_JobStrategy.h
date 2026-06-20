// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Strategy/DPStrategy.h"
#include "GameplayTagContainer.h"
#include "Curves/CurveFloat.h"
#include "Brain/SimAg_BrainTypes.h"
#include "SimAg_JobStrategy.generated.h"

/**
 * Utility-AI strategy: "take a job". Scores high when an open posting of JobKind exists near the agent
 * (queried side-effect-free via the ISimAg_JobProvider seam off Context.Owner) and resolves to CLAIMING
 * the best posting and writing its location as the move target.
 *
 * SCORING is side-effect-free: ScoreFor resolves the job provider seam each evaluation, calls
 * QueryBestJobFor (which does NOT mutate the board), and maps the candidate's relevance — its priority
 * and distance via the designer-authored DesirabilityCurve — to a utility score. EXECUTE is where the
 * side effect lives: it calls ClaimJob (authority), records the claimed job on the agent, and writes the
 * job's WorldLocation into the blackboard MoveTarget. No hardcoded weights.
 */
UCLASS(EditInlineNew, DefaultToInstanced, meta = (DisplayName = "SimAg Job Strategy"))
class DESIGNPATTERNSSIMAGENTS_API USimAg_JobStrategy : public UDP_Strategy
{
	GENERATED_BODY()

public:
	USimAg_JobStrategy();

	//~ Begin UDP_Strategy
	virtual float ScoreFor_Implementation(const FDP_StrategyContext& Context) const override;
	virtual void Execute_Implementation(const FDP_StrategyContext& Context) override;
	//~ End UDP_Strategy

	/** Kind of work this strategy seeks (child of a project's job-kind root). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Brain")
	FGameplayTag JobKind;

	/** Activity tag set on the agent while working this job (e.g. "SimAg.Activity.Work"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Brain", meta = (Categories = "SimAg.Activity"))
	FGameplayTag WorkActivity;

	/**
	 * Maps a candidate job's DISTANCE (X, world units, from the agent to the posting) to a desirability
	 * multiplier (Y). A falling curve makes nearer jobs more attractive; the posting's own Priority is
	 * multiplied in. The curve is the entire distance tuning, so there are no magic weights in code.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Brain")
	FRuntimeFloatCurve DesirabilityCurve;

	/** Blackboard key the claimed job's world location is written into (FVector). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Brain")
	FName MoveTargetKey = TEXT("MoveTarget");
};
