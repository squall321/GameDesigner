// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Strategy/DPStrategy.h"
#include "Curves/CurveFloat.h"
#include "Targeting/Cam_TargetingTypes.h"
#include "Cam_TargetSelectionStrategy.generated.h"

/**
 * Strategy-pattern base for picking the "best" lock-on candidate.
 *
 * Reuses the core Strategy contract (UDP_Strategy) so selection policy is designer-authored,
 * inline-editable and swappable in the editor — but adds a candidate-aware scoring entry point.
 * The base UDP_Strategy::ScoreFor/Execute take only the actor+blackboard context, which is too
 * coarse for per-candidate targeting; ScoreCandidate is the targeting-specific scoring API the
 * UCam_TargetingComponent calls once per candidate, picking the highest score.
 *
 * Higher score is better; a score <= 0 means "reject this candidate". Scoring is side-effect free.
 */
UCLASS(Abstract, EditInlineNew, DefaultToInstanced, meta = (DisplayName = "Cam Target Selection Strategy"))
class DESIGNPATTERNSCAMERA_API UCam_TargetSelectionStrategy : public UDP_Strategy
{
	GENERATED_BODY()

public:
	/**
	 * Score one candidate for the given viewer framing. Higher = preferred; <= 0 = reject.
	 * Must be pure (no side effects) so the component can poll every candidate freely.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "DesignPatterns|Camera|Targeting")
	float ScoreCandidate(const FCam_TargetCandidate& Candidate, const FCam_TargetingView& View) const;
	virtual float ScoreCandidate_Implementation(const FCam_TargetCandidate& Candidate, const FCam_TargetingView& View) const;

	/**
	 * Stickiness bonus added to the candidate that is already the current target, expressed as a
	 * fraction of the candidate's own score (e.g. 0.15 = +15%). Prevents target flicker between two
	 * near-equal candidates. Applied by the component, not inside ScoreCandidate, so it composes with
	 * any subclass policy. Tunable; defensive default keeps a mild hysteresis.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Targeting", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float CurrentTargetStickinessBonus = 0.15f;

protected:
	//~ Begin UDP_Strategy
	/**
	 * Bridges the coarse base contract to targeting: returns 1.0 ("applicable") so this strategy can
	 * still participate in a generic selector if ever placed in one. Real work is ScoreCandidate.
	 */
	virtual float ScoreFor_Implementation(const FDP_StrategyContext& Context) const override;
	//~ End UDP_Strategy
};

/**
 * Selects the geometrically NEAREST candidate. Score is a falloff of distance normalized by the
 * view's MaxRange, shaped by an optional curve so designers can bias near/far without code.
 */
UCLASS(EditInlineNew, DefaultToInstanced, meta = (DisplayName = "Cam Closest Target"))
class DESIGNPATTERNSCAMERA_API UCam_ClosestTargetStrategy : public UCam_TargetSelectionStrategy
{
	GENERATED_BODY()

public:
	virtual float ScoreCandidate_Implementation(const FCam_TargetCandidate& Candidate, const FCam_TargetingView& View) const override;

	/**
	 * Maps normalized distance (X = Distance / MaxRange, in [0,1]) to a score (Y). When unset the
	 * strategy uses a built-in linear (1 - X) falloff. The curve IS the tuning — no magic weights.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Targeting")
	FRuntimeFloatCurve DistanceFalloff;
};

/**
 * Selects the candidate MOST CENTERED in the view (smallest angle from view forward). Score is a
 * falloff of the angle normalized by the acquisition half-angle, shaped by an optional curve.
 */
UCLASS(EditInlineNew, DefaultToInstanced, meta = (DisplayName = "Cam Most-Centered Target"))
class DESIGNPATTERNSCAMERA_API UCam_MostCenteredTargetStrategy : public UCam_TargetSelectionStrategy
{
	GENERATED_BODY()

public:
	virtual float ScoreCandidate_Implementation(const FCam_TargetCandidate& Candidate, const FCam_TargetingView& View) const override;

	/**
	 * Maps normalized angle (X = AngleFromViewDeg / HalfAngleDeg, in [0,1]) to a score (Y). When
	 * unset uses a built-in linear (1 - X) falloff. Lets designers prefer dead-center sharply or softly.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Targeting")
	FRuntimeFloatCurve AngleFalloff;

	/**
	 * Optional small weight given to closeness so two equally-centered candidates break the tie by
	 * distance. Fraction in [0,1] blended into the final score. Tunable; defensive default is a light tie-break.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Targeting", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DistanceTieBreakWeight = 0.1f;
};

/**
 * Selects the HIGHEST-THREAT candidate. Threat is read from the shared blackboard (via the
 * IDP_BlackboardProvider seam already in FDP_StrategyContext) keyed per-entity, so NO threat module
 * is hard-included; a project that has no threat system simply yields zero threat and this strategy
 * falls back to a closeness score so it still produces a sensible pick.
 */
UCLASS(EditInlineNew, DefaultToInstanced, meta = (DisplayName = "Cam Highest-Threat Target"))
class DESIGNPATTERNSCAMERA_API UCam_HighestThreatTargetStrategy : public UCam_TargetSelectionStrategy
{
	GENERATED_BODY()

public:
	virtual float ScoreCandidate_Implementation(const FCam_TargetCandidate& Candidate, const FCam_TargetingView& View) const override;

	/**
	 * Blackboard key prefix the per-entity threat value is stored under. The strategy reads the float
	 * key "ThreatKeyPrefix" + EntityId (digits) from the provider; designers can rename to match their
	 * threat system's convention. No magic key hardcoded inline — it is a tunable.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Targeting")
	FName ThreatKeyPrefix = TEXT("Threat.");

	/**
	 * Maps raw threat (X) to a score (Y). When unset, threat is passed through unchanged. Lets a
	 * project define what "threat" magnitude means without touching code.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Targeting")
	FRuntimeFloatCurve ThreatResponse;

	/**
	 * Weight of the closeness fallback blended in when threat is zero/unavailable, so the strategy
	 * still picks a reasonable target in projects without a threat system. Fraction in [0,1].
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Targeting", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ClosenessFallbackWeight = 0.25f;
};
