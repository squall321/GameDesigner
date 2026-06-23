// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Strategy/DPStrategy.h"
#include "GameplayTagContainer.h"
#include "Brain/SimAg_BrainTypes.h"
#include "SimAg_InterruptStrategy.generated.h"

/**
 * Highest-priority flee/interrupt strategy. Scores VERY HIGH when a threat is sensed near the agent —
 * either through the ISeam_ThreatSense seam (resolved under a project service key) or a remembered Threat
 * fact in USimAg_MemoryComponent. EXECUTE writes a flee MoveTarget (away from the threat) and calls
 * USimAg_RoutineComponent::Interrupt so the agent abandons its daily routine; when no threat remains it
 * triggers Resume so the agent picks its routine back up.
 *
 * Author this FIRST in a UDP_PrioritySelector (or give it a dominating score in a UDP_HighestScoreSelector)
 * so flee always pre-empts ordinary behaviour. Side-effect-free scoring (const seam/memory reads).
 */
UCLASS(EditInlineNew, DefaultToInstanced, meta = (DisplayName = "SimAg Interrupt/Flee Strategy"))
class DESIGNPATTERNSSIMAGENTS_API USimAg_InterruptStrategy : public UDP_Strategy
{
	GENERATED_BODY()

public:
	USimAg_InterruptStrategy();

	//~ Begin UDP_Strategy
	virtual float ScoreFor_Implementation(const FDP_StrategyContext& Context) const override;
	virtual void Execute_Implementation(const FDP_StrategyContext& Context) override;
	//~ End UDP_Strategy

	/** Service key under which the ISeam_ThreatSense provider is registered (project-defined). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Brain", meta = (Categories = "DP.Service"))
	FGameplayTag ThreatServiceKey;

	/** Memory subject kind under which threats are remembered (child of SimAg.Memory). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Brain", meta = (Categories = "SimAg.Memory"))
	FGameplayTag ThreatMemoryKind;

	/** Activity tag set on the agent while fleeing (child of SimAg.Activity). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Brain", meta = (Categories = "SimAg.Activity"))
	FGameplayTag FleeActivity;

	/** Interrupt reason tag passed to USimAg_RoutineComponent::Interrupt. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Brain")
	FGameplayTag InterruptReason;

	/** Radius (world units) within which a threat is sensed via the seam. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Brain", meta = (ClampMin = "1.0"))
	float ThreatSenseRadius = 1500.f;

	/** How far (world units) to flee from the threat when one is found. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Brain", meta = (ClampMin = "1.0"))
	float FleeDistance = 1200.f;

	/**
	 * Score returned while a threat is present. Authored high so the strategy dominates the selector.
	 * (Pure designer weight — there is no hidden magic constant in the flee logic.)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Brain", meta = (ClampMin = "0.0"))
	float ThreatScore = 1000.f;

	/** Blackboard key the flee world target is written into (FVector). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Brain")
	FName MoveTargetKey = TEXT("MoveTarget");

	/** Blackboard bool key recording whether the agent is currently in a flee (so Execute can Resume). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Brain")
	FName FleeingKey = TEXT("IsFleeing");
};
