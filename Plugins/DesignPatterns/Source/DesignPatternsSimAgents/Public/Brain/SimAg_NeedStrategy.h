// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Strategy/DPStrategy.h"
#include "GameplayTagContainer.h"
#include "Curves/CurveFloat.h"
#include "Brain/SimAg_BrainTypes.h"
#include "SimAg_NeedStrategy.generated.h"

/**
 * Utility-AI strategy: "satisfy a need". Scores high when the agent's NeedTag is depleted and resolves
 * to writing the satisfying destination (home/work or an explicit point) onto the blackboard.
 *
 * SCORING is side-effect-free: ScoreFor resolves the ISeam_NeedProvider off Context.Owner each
 * evaluation, reads the normalized need value, and maps its URGENCY (1 - satisfaction) through the
 * designer-authored UrgencyCurve. No hardcoded weights — the curve IS the tuning. EXECUTE writes the
 * chosen MoveTarget into the blackboard and the active activity tag onto the agent component, for
 * steering and downstream systems to pick up.
 */
UCLASS(EditInlineNew, DefaultToInstanced, meta = (DisplayName = "SimAg Need Strategy"))
class DESIGNPATTERNSSIMAGENTS_API USimAg_NeedStrategy : public UDP_Strategy
{
	GENERATED_BODY()

public:
	USimAg_NeedStrategy();

	//~ Begin UDP_Strategy
	virtual float ScoreFor_Implementation(const FDP_StrategyContext& Context) const override;
	virtual void Execute_Implementation(const FDP_StrategyContext& Context) override;
	//~ End UDP_Strategy

	/** The need this strategy addresses (child of SimAg.Need). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Brain", meta = (Categories = "SimAg.Need"))
	FGameplayTag NeedTag;

	/**
	 * Maps need URGENCY (X = 1 - normalized satisfaction, in [0,1]) to a utility score (Y). A rising
	 * curve makes the agent act only once the need is fairly depleted; the shape is the entire tuning,
	 * so there are no magic weights in code.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Brain")
	FRuntimeFloatCurve UrgencyCurve;

	/**
	 * Activity tag this strategy represents (e.g. "SimAg.Activity.Eat"). Written onto the agent's
	 * CurrentActivity on Execute so the agent reflects the chosen behaviour.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Brain", meta = (Categories = "SimAg.Activity"))
	FGameplayTag ActivityTag;

	/**
	 * Where satisfying this need happens. Home/Work read the agent seam's anchors; ExplicitTarget uses
	 * TargetOverride; CurrentLocation keeps the agent in place. Drives the MoveTarget on Execute.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Brain")
	ESimAg_NeedTargetSource TargetSource = ESimAg_NeedTargetSource::Home;

	/** Explicit world target used when TargetSource is ExplicitTarget. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Brain", meta = (EditCondition = "TargetSource == ESimAg_NeedTargetSource::ExplicitTarget"))
	FVector TargetOverride = FVector::ZeroVector;

	/** Blackboard key the chosen world move target is written into (FVector). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Brain")
	FName MoveTargetKey = TEXT("MoveTarget");
};
