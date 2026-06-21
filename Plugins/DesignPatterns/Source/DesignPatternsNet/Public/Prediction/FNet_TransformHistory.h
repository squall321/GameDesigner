// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FNet_TransformHistory.generated.h"

/** One timestamped transform sample in a local interpolation buffer. NOT replicated as a history. */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSNET_API FNet_TransformSample
{
	GENERATED_BODY()

	/** Local receive time (this machine's world seconds) when the sample arrived / was recorded. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Net|Interp")
	double TimeSeconds = 0.0;

	/** World-space location at the sample time. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Net|Interp")
	FVector Location = FVector::ZeroVector;

	/** World-space rotation at the sample time. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Net|Interp")
	FRotator Rotation = FRotator::ZeroRotator;

	/** Linear velocity estimate (for extrapolation when no fresh sample is available). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Net|Interp")
	FVector Velocity = FVector::ZeroVector;

	FNet_TransformSample() = default;
};

/**
 * A purely LOCAL ring of recent transform samples used to smooth a simulated proxy. CRUCIALLY this is
 * never replicated as a whole — only the latest snapshot crosses the wire (a single OnRep on the owning
 * component); every receiver rebuilds its own buffer here from the snapshots it observes. This avoids the
 * classic anti-pattern of replicating a transform history array.
 *
 * The buffer supports two render modes:
 *   - Interpolation: render the proxy at (now - InterpDelay), blending between the two bracketing samples,
 *     which hides jitter at the cost of a fixed visual latency (the standard client-side smoothing trade).
 *   - Extrapolation (dead reckoning): when the newest sample is older than the render time (a dropped /
 *     late packet), advance the last sample by its velocity for up to MaxExtrapolation seconds before
 *     freezing, so a brief hitch glides instead of snapping.
 */
USTRUCT()
struct DESIGNPATTERNSNET_API FNet_TransformHistory
{
	GENERATED_BODY()

	/** Newest-last ring of samples. Bounded by Capacity; oldest are evicted. */
	UPROPERTY(Transient)
	TArray<FNet_TransformSample> Samples;

	/** Maximum samples retained. A few hundred ms at the snapshot rate is plenty. */
	UPROPERTY(Transient)
	int32 Capacity = 32;

	/** Append a sample (computing a velocity estimate from the previous one) and evict overflow. */
	void Push(double TimeSeconds, const FVector& Location, const FRotator& Rotation);

	/** Drop everything (e.g. on teleport, so the next sample starts a fresh buffer). */
	void Reset() { Samples.Reset(); }

	bool IsEmpty() const { return Samples.Num() == 0; }

	/** The most recent sample time, or 0 if empty. */
	double NewestTime() const { return Samples.Num() ? Samples.Last().TimeSeconds : 0.0; }

	/**
	 * Sample the buffer at RenderTime, interpolating between the bracketing samples. If RenderTime is newer
	 * than the last sample, extrapolate by velocity up to MaxExtrapolation seconds, then freeze. Returns
	 * false (and leaves outputs untouched) when the buffer is empty.
	 */
	bool Sample(double RenderTime, double MaxExtrapolation, FVector& OutLocation, FRotator& OutRotation) const;
};
