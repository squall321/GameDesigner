// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Strategy/DPStrategy.h"
#include "GameplayTagContainer.h"
#include "Curves/CurveFloat.h"
#include "Brain/SimAg_BrainTypes.h"
#include "SimAg_SocializeStrategy.generated.h"

/**
 * Utility-AI strategy: "go socialize with a liked agent". Scores by combining the agent's SOCIAL need
 * (urgency via ISeam_NeedProvider) with the best AFFINITY among remembered nearby agents
 * (USimAg_SocialComponent), reading the partner's last-known position from USimAg_MemoryComponent.
 * EXECUTE writes the partner's location to the blackboard MoveTarget.
 *
 * SCORING is side-effect-free (const reads of need / social / memory). Scores 0 when there's no social
 * need, no liked partner, or no memory of where to find them. No hardcoded weights — the curves tune it.
 */
UCLASS(EditInlineNew, DefaultToInstanced, meta = (DisplayName = "SimAg Socialize Strategy"))
class DESIGNPATTERNSSIMAGENTS_API USimAg_SocializeStrategy : public UDP_Strategy
{
	GENERATED_BODY()

public:
	USimAg_SocializeStrategy();

	//~ Begin UDP_Strategy
	virtual float ScoreFor_Implementation(const FDP_StrategyContext& Context) const override;
	virtual void Execute_Implementation(const FDP_StrategyContext& Context) override;
	//~ End UDP_Strategy

	/** The social need this strategy satisfies (child of SimAg.Need). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Brain", meta = (Categories = "SimAg.Need"))
	FGameplayTag SocialNeedTag;

	/** Memory subject kind under which other agents' last-known positions are remembered (SimAg.Memory). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Brain", meta = (Categories = "SimAg.Memory"))
	FGameplayTag AgentMemoryKind;

	/** Activity tag set on the agent on Execute (child of SimAg.Activity). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Brain", meta = (Categories = "SimAg.Activity"))
	FGameplayTag ActivityTag;

	/**
	 * Maps need URGENCY (X = 1 - social satisfaction, [0,1]) to a base score (Y). A rising curve makes
	 * the agent seek company once lonely. The shape is the tuning.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Brain")
	FRuntimeFloatCurve UrgencyCurve;

	/**
	 * Maps best partner AFFINITY (X in [-1,1]) to a multiplier (Y) on the urgency score. A rising curve
	 * makes the agent prefer well-liked partners and avoid disliked ones. The shape is the tuning.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Brain")
	FRuntimeFloatCurve AffinityCurve;

	/** Minimum affinity a remembered agent must have to be considered a socialization partner. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Brain", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float MinPartnerAffinity = 0.f;

	/** Blackboard key the partner's world location is written into (FVector). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Brain")
	FName MoveTargetKey = TEXT("MoveTarget");
};
