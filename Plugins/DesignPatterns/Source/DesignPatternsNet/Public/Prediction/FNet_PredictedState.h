// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Net/Seam_NetValue.h"
#include "FNet_PredictedState.generated.h"

/**
 * One client input sample in the prediction ring. Carries a monotonically increasing sequence number
 * (the server acks the highest it has processed), the delta-time the client simulated it over, and the
 * opaque input payload as an FSeam_NetValue (the closed, net-safe variant — a raw FInstancedStruct must
 * never cross the wire). Games encode their per-frame input (move axis vector, button bitfield, etc.)
 * into the FSeam_NetValue; the component is input-type-agnostic.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSNET_API FNet_PredictedInput
{
	GENERATED_BODY()

	/** Monotonic client sequence number. The server processes inputs in order and acks the highest. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Net|Prediction")
	uint32 Sequence = 0;

	/** The client-side delta time this input was simulated over (seconds), for deterministic re-sim. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Net|Prediction")
	float DeltaTime = 0.f;

	/** The opaque, net-safe input payload (game-defined encoding). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Net|Prediction")
	FSeam_NetValue Payload;

	FNet_PredictedInput() = default;
	FNet_PredictedInput(uint32 InSeq, float InDt, const FSeam_NetValue& InPayload)
		: Sequence(InSeq), DeltaTime(InDt), Payload(InPayload) {}
};

/**
 * The server's authoritative reconciliation snapshot, replicated to the owning client. It pairs the
 * highest input sequence the server has applied (AckedSequence) with the resulting authoritative state
 * (again as an FSeam_NetValue so it can replicate compactly). The client compares its locally-predicted
 * state at that sequence against State; if they diverge beyond a tolerance it snaps to State and
 * re-simulates every still-unacked input forward (the classic predict -> reconcile loop).
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSNET_API FNet_PredictedSnapshot
{
	GENERATED_BODY()

	/** Highest client input sequence the server has authoritatively applied. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Net|Prediction")
	uint32 AckedSequence = 0;

	/** Authoritative server state AT AckedSequence, against which the client reconciles. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Net|Prediction")
	FSeam_NetValue State;

	/** Monotonic server stamp (server world seconds) when the snapshot was produced, for diagnostics. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Net|Prediction")
	double ServerTimeSeconds = 0.0;

	FNet_PredictedSnapshot() = default;

	/** Custom NetSerialize so the snapshot replicates compactly (delegates to FSeam_NetValue's). */
	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess);

	bool operator==(const FNet_PredictedSnapshot& Other) const
	{
		return AckedSequence == Other.AckedSequence && State == Other.State;
	}
};

template<>
struct TStructOpsTypeTraits<FNet_PredictedSnapshot> : public TStructOpsTypeTraitsBase2<FNet_PredictedSnapshot>
{
	enum
	{
		WithNetSerializer = true,
		WithIdenticalViaEquality = true,
	};
};
