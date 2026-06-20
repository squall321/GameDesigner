// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Targeting/Cam_TargetSelectionStrategy.h"
#include "FSM/DPBlackboard.h"
#include "Core/DPLog.h"

namespace
{
	/** Clamp X to [0,1], then evaluate Curve if it has keys, otherwise return Fallback(X). */
	float EvalNormalizedCurve(const FRuntimeFloatCurve& Curve, float NormalizedX, TFunctionRef<float(float)> Fallback)
	{
		const float X = FMath::Clamp(NormalizedX, 0.f, 1.f);
		const FRichCurve* Rich = Curve.GetRichCurveConst();
		if (Rich && Rich->GetNumKeys() > 0)
		{
			return Rich->Eval(X);
		}
		return Fallback(X);
	}

	/** Normalized closeness in [0,1] from a candidate distance and the view range (1 = at the viewer). */
	float ComputeCloseness(const FCam_TargetCandidate& Candidate, const FCam_TargetingView& View)
	{
		const float Range = View.MaxRange > KINDA_SMALL_NUMBER ? View.MaxRange : 1.f;
		const float NormDist = FMath::Clamp(Candidate.Distance / Range, 0.f, 1.f);
		return 1.f - NormDist;
	}
}

//------------------------------------------------------------------------------------------------
// UCam_TargetSelectionStrategy (base)
//------------------------------------------------------------------------------------------------

float UCam_TargetSelectionStrategy::ScoreFor_Implementation(const FDP_StrategyContext& Context) const
{
	// Targeting scoring is per-candidate via ScoreCandidate; report "applicable" for generic selectors.
	return 1.f;
}

float UCam_TargetSelectionStrategy::ScoreCandidate_Implementation(const FCam_TargetCandidate& Candidate, const FCam_TargetingView& View) const
{
	// Base policy: any id-bearing candidate is equally valid. Subclasses refine.
	return Candidate.EntityId.IsValid() ? 1.f : 0.f;
}

//------------------------------------------------------------------------------------------------
// UCam_ClosestTargetStrategy
//------------------------------------------------------------------------------------------------

float UCam_ClosestTargetStrategy::ScoreCandidate_Implementation(const FCam_TargetCandidate& Candidate, const FCam_TargetingView& View) const
{
	if (!Candidate.EntityId.IsValid())
	{
		return 0.f;
	}
	// Normalize distance by MaxRange; closer = higher. Built-in fallback falloff is linear (1 - X);
	// the curve, when authored, overrides the shape entirely.
	const float Range = View.MaxRange > KINDA_SMALL_NUMBER ? View.MaxRange : 1.f;
	const float NormDist = Candidate.Distance / Range;
	const float Score = EvalNormalizedCurve(DistanceFalloff, NormDist, [](float X) { return 1.f - X; });
	return FMath::Max(0.f, Score);
}

//------------------------------------------------------------------------------------------------
// UCam_MostCenteredTargetStrategy
//------------------------------------------------------------------------------------------------

float UCam_MostCenteredTargetStrategy::ScoreCandidate_Implementation(const FCam_TargetCandidate& Candidate, const FCam_TargetingView& View) const
{
	if (!Candidate.EntityId.IsValid())
	{
		return 0.f;
	}
	const float HalfAngle = View.HalfAngleDeg > KINDA_SMALL_NUMBER ? View.HalfAngleDeg : 1.f;
	const float NormAngle = Candidate.AngleFromViewDeg / HalfAngle;
	const float CenterScore = EvalNormalizedCurve(AngleFalloff, NormAngle, [](float X) { return 1.f - X; });

	// Center dominates; closeness only breaks ties. With W the tie-break weight, the closeness term
	// is gated by CenterScore so a far dead-center target still beats a near off-center one.
	const float Closeness = ComputeCloseness(Candidate, View);
	const float W = FMath::Clamp(DistanceTieBreakWeight, 0.f, 1.f);
	const float Blended = CenterScore * (1.f - W) + (CenterScore * Closeness) * W;
	return FMath::Max(0.f, Blended);
}

//------------------------------------------------------------------------------------------------
// UCam_HighestThreatTargetStrategy
//------------------------------------------------------------------------------------------------

float UCam_HighestThreatTargetStrategy::ScoreCandidate_Implementation(const FCam_TargetCandidate& Candidate, const FCam_TargetingView& View) const
{
	if (!Candidate.EntityId.IsValid())
	{
		return 0.f;
	}

	const float Closeness = ComputeCloseness(Candidate, View);

	// Read this candidate's threat from the shared blackboard via the seam, if one is bound. The key
	// is ThreatKeyPrefix + the entity id's hyphenated digits, so threat is per-entity. No threat
	// module is referenced: when the provider/key is absent GetFloat returns 0 and we fall back to
	// closeness so a sensible target is still chosen.
	float Threat = 0.f;
	bool bHaveThreatProvider = false;
	if (IDP_BlackboardProvider* Provider = View.Blackboard.GetInterface())
	{
		const FName Key(*(ThreatKeyPrefix.ToString() + Candidate.EntityId.ToString()));
		if (Provider->HasKey(Key))
		{
			Threat = Provider->GetFloat(Key, 0.f);
			bHaveThreatProvider = true;
		}
	}

	const FRichCurve* Rich = ThreatResponse.GetRichCurveConst();
	const float ThreatScore = (Rich && Rich->GetNumKeys() > 0) ? Rich->Eval(Threat) : Threat;

	if (!bHaveThreatProvider || ThreatScore <= 0.f)
	{
		// No usable threat info: degrade to a closeness-driven pick.
		return FMath::Max(0.f, Closeness);
	}

	const float W = FMath::Clamp(ClosenessFallbackWeight, 0.f, 1.f);
	const float Blended = ThreatScore * (1.f - W) + Closeness * W;
	return FMath::Max(0.f, Blended);
}
