// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Strategy/DPStrategy.h"
#include "GameplayTagContainer.h"
#include "AI_PositioningStrategies.generated.h"

class UAI_EnvQuery;

/**
 * Abstract base for tactical MOVE-TARGET strategies that drop directly into the existing
 * UDP_StrategySelector arrays on FSM states. ScoreFor reads the FDP_StrategyContext blackboard (the
 * IDP_BlackboardProvider the brain/perception already populate) for AI.TargetLocation; Execute computes
 * a destination — optionally via UAI_QuerySubsystem — and writes it to BlackboardKey_MoveTarget (which
 * defaults to AI.TargetLocation, the key the existing brain/perception/cover already use). It NEVER adds
 * a new brain method: the destination is communicated purely through the shared blackboard.
 *
 * All concrete leaves are designer-tunable (no magic gameplay numbers in code).
 */
UCLASS(Abstract)
class DESIGNPATTERNSAI_API UAI_PositioningStrategy : public UDP_Strategy
{
	GENERATED_BODY()

public:
	/**
	 * Optional EQS-style query used to refine/validate the computed destination. When set, the strategy
	 * runs it (centered on the raw destination) through UAI_QuerySubsystem and uses the best scored point.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Positioning")
	TObjectPtr<UAI_EnvQuery> PlacementQuery;

	/** Blackboard key the computed move target is written to. Defaults to AI.TargetLocation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Positioning")
	FName BlackboardKey_MoveTarget = TEXT("AI.TargetLocation");

	/**
	 * Blackboard key the strategy reads to learn where the THREAT/target is (so it can position relative
	 * to it). Defaults to AI.TargetLocation (written by perception). When this equals BlackboardKey_MoveTarget,
	 * the strategy reads the threat first, then overwrites with its computed destination.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Positioning")
	FName BlackboardKey_ThreatLocation = TEXT("AI.TargetLocation");

	//~ Begin UDP_Strategy
	virtual float ScoreFor_Implementation(const FDP_StrategyContext& Context) const override;
	virtual void Execute_Implementation(const FDP_StrategyContext& Context) override;
	//~ End UDP_Strategy

protected:
	/**
	 * Compute the raw desired world destination for this strategy given owner + threat locations. Subclasses
	 * override this; the base Execute then runs PlacementQuery (if any) and writes the result.
	 * @return true if a destination was produced (false skips writing).
	 */
	virtual bool ComputeDestination(const FDP_StrategyContext& Context, const FVector& OwnerLocation,
		const FVector& ThreatLocation, FVector& OutDestination) const;

	/** Read the threat/target world location from the blackboard (zero if absent). */
	FVector ReadThreatLocation(const FDP_StrategyContext& Context) const;

	/** Resolve the owner's world location (zero if no owner). */
	FVector ResolveOwnerLocation(const FDP_StrategyContext& Context) const;

	/** Run PlacementQuery (if set) centered on RawDestination, returning the best point or RawDestination. */
	FVector RefineWithQuery(const FDP_StrategyContext& Context, const FVector& RawDestination, const FVector& ThreatLocation) const;
};

/**
 * Move to a point offset to the SIDE of the threat by FlankAngleDegrees, at PreferredRange, so the agent
 * approaches from an angle rather than head-on.
 */
UCLASS(meta = (DisplayName = "Flank"))
class DESIGNPATTERNSAI_API UAI_FlankStrategy : public UAI_PositioningStrategy
{
	GENERATED_BODY()

public:
	/** Angle (degrees) to rotate the owner->threat approach vector by, producing a flanking offset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Positioning|Flank", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float FlankAngleDegrees = 60.f;

	/** Desired distance (world units) to hold from the threat at the flank point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Positioning|Flank", meta = (ClampMin = "1.0"))
	float PreferredRange = 400.f;

protected:
	virtual bool ComputeDestination(const FDP_StrategyContext& Context, const FVector& OwnerLocation,
		const FVector& ThreatLocation, FVector& OutDestination) const override;
};

/**
 * SURROUND: distribute squadmates evenly around the threat. Enumerates the squad via IAI_Squad::GetMembers
 * (resolved from DP.Service.AI.Squad), finds this agent's index, and assigns it a slot on a ring of radius
 * PreferredRange around the threat at angle (index / count) * 360°.
 */
UCLASS(meta = (DisplayName = "Surround"))
class DESIGNPATTERNSAI_API UAI_SurroundStrategy : public UAI_PositioningStrategy
{
	GENERATED_BODY()

public:
	/** Ring radius (world units) around the threat the surrounding agents hold. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Positioning|Surround", meta = (ClampMin = "1.0"))
	float PreferredRange = 400.f;

protected:
	virtual bool ComputeDestination(const FDP_StrategyContext& Context, const FVector& OwnerLocation,
		const FVector& ThreatLocation, FVector& OutDestination) const override;
};

/**
 * KITE: maintain distance — if the threat is closer than MinThreatDistance, pick a point directly away
 * from it at PreferredRange; otherwise hold position (score low so other strategies win).
 */
UCLASS(meta = (DisplayName = "Kite"))
class DESIGNPATTERNSAI_API UAI_KiteStrategy : public UAI_PositioningStrategy
{
	GENERATED_BODY()

public:
	/** If the threat is within this distance (world units), the agent retreats to PreferredRange. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Positioning|Kite", meta = (ClampMin = "0.0"))
	float MinThreatDistance = 300.f;

	/** Distance (world units) the agent wants to hold from the threat while kiting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Positioning|Kite", meta = (ClampMin = "1.0"))
	float PreferredRange = 600.f;

	//~ Begin UDP_Strategy
	/** Scores high only when the threat is closer than MinThreatDistance (otherwise no need to kite). */
	virtual float ScoreFor_Implementation(const FDP_StrategyContext& Context) const override;
	//~ End UDP_Strategy

protected:
	virtual bool ComputeDestination(const FDP_StrategyContext& Context, const FVector& OwnerLocation,
		const FVector& ThreatLocation, FVector& OutDestination) const override;
};

/**
 * COVER-ADVANCE: move to the best cover point between the agent and the threat, resolving the cover provider
 * seam (ISeam_CoverProvider) from DP.Service.AI.Cover. Falls back to a direct advance toward the threat at
 * PreferredRange when no cover is available.
 */
UCLASS(meta = (DisplayName = "Cover Advance"))
class DESIGNPATTERNSAI_API UAI_CoverAdvanceStrategy : public UAI_PositioningStrategy
{
	GENERATED_BODY()

public:
	/** Search radius (world units) for cover around the agent. <= 0 means "no radius limit". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Positioning|Cover", meta = (ClampMin = "0.0"))
	float CoverSearchRadius = 1200.f;

	/** Distance (world units) to advance toward the threat when no cover is available (fallback). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Positioning|Cover", meta = (ClampMin = "1.0"))
	float PreferredRange = 500.f;

protected:
	virtual bool ComputeDestination(const FDP_StrategyContext& Context, const FVector& OwnerLocation,
		const FVector& ThreatLocation, FVector& OutDestination) const override;
};
