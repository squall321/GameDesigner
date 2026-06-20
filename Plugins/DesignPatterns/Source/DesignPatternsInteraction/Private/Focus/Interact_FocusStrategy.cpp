// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Focus/Interact_FocusStrategy.h"

//~ UInteract_FocusStrategy (base) ------------------------------------------------------------

int32 UInteract_FocusStrategy::SelectBestCandidate_Implementation(
	const TArray<FInteract_Candidate>& Candidates, const FInteract_Query& Query) const
{
	// Base default: the first valid candidate, or none. Concrete strategies override with policy.
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		if (Candidates[Index].IsValidCandidate())
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

//~ UInteract_FocusStrategy_Closest -----------------------------------------------------------

int32 UInteract_FocusStrategy_Closest::SelectBestCandidate_Implementation(
	const TArray<FInteract_Candidate>& Candidates, const FInteract_Query& Query) const
{
	int32 BestIndex = INDEX_NONE;
	float BestDistance = TNumericLimits<float>::Max();
	float BestAngle = TNumericLimits<float>::Max();

	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		const FInteract_Candidate& C = Candidates[Index];
		if (!C.IsValidCandidate())
		{
			continue;
		}

		// Closest wins; equal distance broken by the more centered candidate.
		if (C.Distance < BestDistance ||
			(FMath::IsNearlyEqual(C.Distance, BestDistance) && C.AngleDeg < BestAngle))
		{
			BestDistance = C.Distance;
			BestAngle = C.AngleDeg;
			BestIndex = Index;
		}
	}
	return BestIndex;
}

//~ UInteract_FocusStrategy_LineOfSight -------------------------------------------------------

int32 UInteract_FocusStrategy_LineOfSight::SelectBestCandidate_Implementation(
	const TArray<FInteract_Candidate>& Candidates, const FInteract_Query& Query) const
{
	int32 BestLosIndex = INDEX_NONE;
	float BestLosAngle = TNumericLimits<float>::Max();
	float BestLosDistance = TNumericLimits<float>::Max();

	int32 BestAnyIndex = INDEX_NONE;
	float BestAnyDistance = TNumericLimits<float>::Max();

	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		const FInteract_Candidate& C = Candidates[Index];
		if (!C.IsValidCandidate())
		{
			continue;
		}

		// Track an overall-closest fallback for when nothing has line of sight.
		if (C.Distance < BestAnyDistance)
		{
			BestAnyDistance = C.Distance;
			BestAnyIndex = Index;
		}

		if (!C.bHasLineOfSight)
		{
			continue;
		}

		// Among LOS candidates prefer the most centered, then the closest.
		if (C.AngleDeg < BestLosAngle ||
			(FMath::IsNearlyEqual(C.AngleDeg, BestLosAngle) && C.Distance < BestLosDistance))
		{
			BestLosAngle = C.AngleDeg;
			BestLosDistance = C.Distance;
			BestLosIndex = Index;
		}
	}

	return BestLosIndex != INDEX_NONE ? BestLosIndex : BestAnyIndex;
}

//~ UInteract_FocusStrategy_Cone --------------------------------------------------------------

int32 UInteract_FocusStrategy_Cone::SelectBestCandidate_Implementation(
	const TArray<FInteract_Candidate>& Candidates, const FInteract_Query& Query) const
{
	int32 BestIndex = INDEX_NONE;
	float BestAngle = TNumericLimits<float>::Max();
	float BestDistance = TNumericLimits<float>::Max();

	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		const FInteract_Candidate& C = Candidates[Index];
		if (!C.IsValidCandidate())
		{
			continue;
		}

		// Reject anything outside the reticle cone.
		if (C.AngleDeg > MaxConeHalfAngleDeg)
		{
			continue;
		}

		// Most centered wins; equal angle broken by closest.
		if (C.AngleDeg < BestAngle ||
			(FMath::IsNearlyEqual(C.AngleDeg, BestAngle) && C.Distance < BestDistance))
		{
			BestAngle = C.AngleDeg;
			BestDistance = C.Distance;
			BestIndex = Index;
		}
	}
	return BestIndex;
}

//~ UInteract_FocusStrategy_Priority ----------------------------------------------------------

int32 UInteract_FocusStrategy_Priority::SelectBestCandidate_Implementation(
	const TArray<FInteract_Candidate>& Candidates, const FInteract_Query& Query) const
{
	int32 BestIndex = INDEX_NONE;
	int32 BestPriority = TNumericLimits<int32>::Lowest();
	float BestDistance = TNumericLimits<float>::Max();

	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		const FInteract_Candidate& C = Candidates[Index];
		if (!C.IsValidCandidate())
		{
			continue;
		}

		// Highest priority wins; equal priority broken by closest.
		if (C.Priority > BestPriority ||
			(C.Priority == BestPriority && C.Distance < BestDistance))
		{
			BestPriority = C.Priority;
			BestDistance = C.Distance;
			BestIndex = Index;
		}
	}
	return BestIndex;
}
