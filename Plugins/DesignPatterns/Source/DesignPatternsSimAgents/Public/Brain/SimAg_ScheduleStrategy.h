// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Strategy/DPStrategy.h"
#include "GameplayTagContainer.h"
#include "Curves/CurveFloat.h"
#include "Brain/SimAg_BrainTypes.h"
#include "SimAg_ScheduleStrategy.generated.h"

/**
 * Utility-AI strategy: "follow the schedule". Scores high when the agent's scheduled activity (read via
 * the ISimAg_Scheduler seam off Context.Owner) matches this strategy's MatchActivity, and resolves to
 * sending the agent to its work/home anchor for that slot.
 *
 * SCORING is side-effect-free: ScoreFor resolves the scheduler seam each evaluation, reads the current
 * activity, and returns BaseScore (mapped through an optional curve by how recently the slot began is
 * not tracked here — the schedule itself is the timing source). EXECUTE writes the anchor MoveTarget and
 * sets the agent's CurrentActivity to the scheduled activity. No hardcoded weights.
 */
UCLASS(EditInlineNew, DefaultToInstanced, meta = (DisplayName = "SimAg Schedule Strategy"))
class DESIGNPATTERNSSIMAGENTS_API USimAg_ScheduleStrategy : public UDP_Strategy
{
	GENERATED_BODY()

public:
	USimAg_ScheduleStrategy();

	//~ Begin UDP_Strategy
	virtual float ScoreFor_Implementation(const FDP_StrategyContext& Context) const override;
	virtual void Execute_Implementation(const FDP_StrategyContext& Context) override;
	//~ End UDP_Strategy

	/**
	 * Schedule activity this strategy responds to (child of SimAg.Activity). The scheduler's current
	 * activity must match this tag (hierarchy-aware) for the strategy to be applicable.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Brain", meta = (Categories = "SimAg.Activity"))
	FGameplayTag MatchActivity;

	/**
	 * Utility produced when the scheduled activity matches. A flat designer value (the schedule decides
	 * WHEN; this decides HOW STRONGLY scheduled behaviour competes with needs/jobs). Tuning lives here,
	 * not in code.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Brain", meta = (ClampMin = "0.0"))
	float MatchScore = 1.f;

	/** Where the scheduled slot sends the agent. Defaults to the work anchor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Brain")
	ESimAg_NeedTargetSource TargetSource = ESimAg_NeedTargetSource::Work;

	/** Explicit world target used when TargetSource is ExplicitTarget. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Brain", meta = (EditCondition = "TargetSource == ESimAg_NeedTargetSource::ExplicitTarget"))
	FVector TargetOverride = FVector::ZeroVector;

	/** Blackboard key the chosen world move target is written into (FVector). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Brain")
	FName MoveTargetKey = TEXT("MoveTarget");
};
