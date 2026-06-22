// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Focus/Interact_FocusStrategy_Weighted.h"

int32 UInteract_FocusStrategy_Weighted::SelectBestCandidate_Implementation(
	const TArray<FInteract_Candidate>& Candidates, const FInteract_Query& Query) const
{
	int32 BestIndex = INDEX_NONE;
	float BestScore = -TNumericLimits<float>::Max();
	float BestDistance = TNumericLimits<float>::Max();

	const float DistNorm = FMath::Max(1.f, DistanceNormalization);
	const int32 PrioNorm = FMath::Max(1, PriorityNormalization);

	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		const FInteract_Candidate& C = Candidates[Index];
		if (!C.IsValidCandidate())
		{
			continue;
		}

		// Inverse-distance term in [0,1]: 1 at the view location, ~0.5 at DistanceNormalization.
		const float DistTerm = DistNorm / (DistNorm + FMath::Max(0.f, C.Distance));

		// Inverse-angle term in [0,1]: 1 dead-centre, 0 at 180 degrees off-axis.
		const float AngleTerm = 1.f - FMath::Clamp(C.AngleDeg / 180.f, 0.f, 1.f);

		// Normalized priority term in [0,1].
		const float PrioTerm = FMath::Clamp(static_cast<float>(C.Priority) / static_cast<float>(PrioNorm), 0.f, 1.f);

		float Score =
			DistanceWeight * DistTerm +
			AngleWeight * AngleTerm +
			PriorityWeight * PrioTerm;

		if (C.bHasLineOfSight)
		{
			Score += LineOfSightBonus;
		}

		// Higher score wins; ties broken by the closer candidate for stable, intuitive focus.
		if (Score > BestScore ||
			(FMath::IsNearlyEqual(Score, BestScore) && C.Distance < BestDistance))
		{
			BestScore = Score;
			BestDistance = C.Distance;
			BestIndex = Index;
		}
	}

	return BestIndex;
}
