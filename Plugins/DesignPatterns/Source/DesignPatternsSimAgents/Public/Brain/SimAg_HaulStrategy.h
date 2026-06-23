// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Strategy/DPStrategy.h"
#include "GameplayTagContainer.h"
#include "Curves/CurveFloat.h"
#include "Brain/SimAg_BrainTypes.h"
#include "Identity/Seam_EntityId.h"
#include "SimAg_HaulStrategy.generated.h"

/**
 * Phase of a two-step haul: fetch the resource from a source, then deliver it to a destination.
 */
UENUM(BlueprintType)
enum class ESimAg_HaulPhase : uint8
{
	/** Not currently hauling. */
	Idle,
	/** Travelling to (and at) the source to pick up the resource. */
	Fetch,
	/** Carrying the resource to the destination. */
	Deliver
};

/**
 * Utility-AI strategy: "haul a resource". A two-phase fetch-then-deliver behaviour. It scores by
 * combining an open haul job (ISimAg_JobProvider::QueryBestJobFor, side-effect-free), the availability of
 * the source via the reservation seam (ISeam_JobReservation::IsReserved), and a remembered stockpile
 * location (USimAg_MemoryComponent). EXECUTE reserves the source through the seam, writes the appropriate
 * MoveTarget for the current phase, and advances a blackboard phase key — it never edits the board
 * directly (it claims via the seam and reserves via the seam).
 *
 * SCORING is side-effect-free. The phase lives on the blackboard (an int) so the behaviour survives across
 * decision passes and the agent finishes a haul before re-deciding.
 */
UCLASS(EditInlineNew, DefaultToInstanced, meta = (DisplayName = "SimAg Haul Strategy"))
class DESIGNPATTERNSSIMAGENTS_API USimAg_HaulStrategy : public UDP_Strategy
{
	GENERATED_BODY()

public:
	USimAg_HaulStrategy();

	//~ Begin UDP_Strategy
	virtual float ScoreFor_Implementation(const FDP_StrategyContext& Context) const override;
	virtual void Execute_Implementation(const FDP_StrategyContext& Context) override;
	//~ End UDP_Strategy

	/** Kind of haul work this strategy seeks (matched against the job board). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Brain")
	FGameplayTag HaulJobKind;

	/** Memory subject kind under which the source stockpile location is remembered (SimAg.Memory). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Brain", meta = (Categories = "SimAg.Memory"))
	FGameplayTag SourceMemoryKind;

	/** Activity tag set on the agent while hauling (child of SimAg.Activity). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Brain", meta = (Categories = "SimAg.Activity"))
	FGameplayTag ActivityTag;

	/**
	 * Maps the destination DISTANCE (X, world units) to a desirability multiplier (Y). A falling curve
	 * prefers nearer deliveries. The shape is the tuning.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Brain")
	FRuntimeFloatCurve DesirabilityCurve;

	/** Blackboard key the per-phase world move target is written into (FVector). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Brain")
	FName MoveTargetKey = TEXT("MoveTarget");

	/** Blackboard key the haul phase (ESimAg_HaulPhase as int) is stored in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Brain")
	FName PhaseKey = TEXT("HaulPhase");

	/** Blackboard key the delivery destination (FVector) is stashed in across the fetch phase. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Brain")
	FName DeliverTargetKey = TEXT("HaulDeliverTarget");

	/** Distance (world units) at which the agent is considered "at" a fetch/deliver point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Brain", meta = (ClampMin = "1.0"))
	float PhaseArrivalRadius = 120.f;
};
