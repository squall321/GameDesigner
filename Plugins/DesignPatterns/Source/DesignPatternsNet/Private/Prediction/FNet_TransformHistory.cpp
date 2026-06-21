// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Prediction/FNet_TransformHistory.h"

void FNet_TransformHistory::Push(double TimeSeconds, const FVector& Location, const FRotator& Rotation)
{
	FNet_TransformSample Sample;
	Sample.TimeSeconds = TimeSeconds;
	Sample.Location = Location;
	Sample.Rotation = Rotation;

	// Estimate velocity from the previous sample so extrapolation has something to dead-reckon with.
	if (Samples.Num() > 0)
	{
		const FNet_TransformSample& Prev = Samples.Last();
		const double Dt = TimeSeconds - Prev.TimeSeconds;
		if (Dt > UE_DOUBLE_KINDA_SMALL_NUMBER)
		{
			Sample.Velocity = (Location - Prev.Location) / Dt;
		}
		else
		{
			Sample.Velocity = Prev.Velocity;
		}
	}

	Samples.Add(Sample);

	const int32 Cap = FMath::Max(2, Capacity);
	while (Samples.Num() > Cap)
	{
		Samples.RemoveAt(0, 1);
	}
}

bool FNet_TransformHistory::Sample(double RenderTime, double MaxExtrapolation, FVector& OutLocation, FRotator& OutRotation) const
{
	if (Samples.Num() == 0)
	{
		return false;
	}

	// Single sample: nothing to blend; return it (optionally extrapolated).
	if (Samples.Num() == 1)
	{
		const FNet_TransformSample& Only = Samples[0];
		const double Ahead = FMath::Clamp(RenderTime - Only.TimeSeconds, 0.0, FMath::Max(0.0, MaxExtrapolation));
		OutLocation = Only.Location + Only.Velocity * Ahead;
		OutRotation = Only.Rotation;
		return true;
	}

	const FNet_TransformSample& Newest = Samples.Last();

	// RenderTime newer than our newest sample -> extrapolate (dead reckon) up to the cap, then freeze.
	if (RenderTime >= Newest.TimeSeconds)
	{
		const double Ahead = FMath::Clamp(RenderTime - Newest.TimeSeconds, 0.0, FMath::Max(0.0, MaxExtrapolation));
		OutLocation = Newest.Location + Newest.Velocity * Ahead;
		OutRotation = Newest.Rotation;
		return true;
	}

	// RenderTime older than our oldest sample -> clamp to the oldest (we have no data that far back).
	const FNet_TransformSample& Oldest = Samples[0];
	if (RenderTime <= Oldest.TimeSeconds)
	{
		OutLocation = Oldest.Location;
		OutRotation = Oldest.Rotation;
		return true;
	}

	// Find the bracketing pair [A, B] with A.Time <= RenderTime <= B.Time and lerp between them.
	for (int32 i = Samples.Num() - 1; i >= 1; --i)
	{
		const FNet_TransformSample& B = Samples[i];
		const FNet_TransformSample& A = Samples[i - 1];
		if (RenderTime >= A.TimeSeconds && RenderTime <= B.TimeSeconds)
		{
			const double Span = B.TimeSeconds - A.TimeSeconds;
			const float Alpha = (Span > UE_DOUBLE_KINDA_SMALL_NUMBER)
				? (float)FMath::Clamp((RenderTime - A.TimeSeconds) / Span, 0.0, 1.0)
				: 1.f;

			OutLocation = FMath::Lerp(A.Location, B.Location, Alpha);
			OutRotation = FMath::Lerp(A.Rotation, B.Rotation, Alpha); // shortest-path rotator lerp
			return true;
		}
	}

	// Fallback (should be unreachable given the clamps above): snap to newest.
	OutLocation = Newest.Location;
	OutRotation = Newest.Rotation;
	return true;
}
