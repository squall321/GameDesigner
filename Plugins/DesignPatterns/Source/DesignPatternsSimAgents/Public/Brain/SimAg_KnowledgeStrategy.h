// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Strategy/DPStrategy.h"
#include "GameplayTagContainer.h"
#include "Curves/CurveFloat.h"
#include "Brain/SimAg_BrainTypes.h"
#include "SimAg_KnowledgeStrategy.generated.h"

/**
 * Utility-AI strategy: "go to a known location". Scores "head toward a remembered <SubjectKind>" by
 * reading the agent's USimAg_MemoryComponent off Context.Owner, combining the strongest decayed
 * confidence with distance through a designer-authored curve. EXECUTE writes the remembered location to
 * the blackboard MoveTarget so steering travels there.
 *
 * SCORING is side-effect-free: it queries memory (a const read) each evaluation. With no memory
 * component, or no confident-enough fact, it scores 0 (not applicable). No hardcoded weights — the curve
 * IS the tuning.
 */
UCLASS(EditInlineNew, DefaultToInstanced, meta = (DisplayName = "SimAg Knowledge Strategy"))
class DESIGNPATTERNSSIMAGENTS_API USimAg_KnowledgeStrategy : public UDP_Strategy
{
	GENERATED_BODY()

public:
	USimAg_KnowledgeStrategy();

	//~ Begin UDP_Strategy
	virtual float ScoreFor_Implementation(const FDP_StrategyContext& Context) const override;
	virtual void Execute_Implementation(const FDP_StrategyContext& Context) override;
	//~ End UDP_Strategy

	/** The kind of remembered thing this strategy heads toward (child of SimAg.Memory). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Brain", meta = (Categories = "SimAg.Memory"))
	FGameplayTag SubjectKind;

	/** Activity tag set on the agent on Execute (child of SimAg.Activity). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Brain", meta = (Categories = "SimAg.Activity"))
	FGameplayTag ActivityTag;

	/**
	 * Maps decayed CONFIDENCE (X in [0,1]) of the best remembered fact to a utility score (Y). A rising
	 * curve makes the agent act on confident memories and ignore faded ones. The shape is the tuning.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Brain")
	FRuntimeFloatCurve ConfidenceCurve;

	/** Blackboard key the chosen world move target is written into (FVector). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Brain")
	FName MoveTargetKey = TEXT("MoveTarget");
};
