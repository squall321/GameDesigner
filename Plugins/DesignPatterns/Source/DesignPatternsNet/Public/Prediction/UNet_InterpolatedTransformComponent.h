// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Prediction/FNet_TransformHistory.h"
#include "UNet_InterpolatedTransformComponent.generated.h"

/**
 * The single replicated transform snapshot: the latest authoritative location + (quantized) rotation,
 * plus a monotonic counter that forces the OnRep to fire even when the transform repeats. This struct is
 * the ONLY networked transform data — a history is rebuilt locally from the stream of these.
 */
USTRUCT()
struct DESIGNPATTERNSNET_API FNet_TransformSnapshot
{
	GENERATED_BODY()

	/** Quantized authoritative world location (cm precision is ample for proxy smoothing). */
	UPROPERTY()
	FVector_NetQuantize100 Location = FVector::ZeroVector;

	/** Authoritative world rotation. */
	UPROPERTY()
	FRotator Rotation = FRotator::ZeroRotator;

	/** Monotonic sequence so a repeated transform still triggers OnRep on the receiver. */
	UPROPERTY()
	uint32 Counter = 0;
};

/**
 * Smooths a simulated proxy's replicated transform on each receiver WITHOUT replicating any transform
 * history. Only the LATEST authoritative snapshot crosses the wire (a single OnRep); every client builds
 * its OWN local interpolation buffer (FNet_TransformHistory) from the snapshots it observes and renders
 * the owning actor at (now - InterpDelay), gliding between samples and dead-reckoning across dropped
 * packets. This is the correct, bandwidth-light alternative to bReplicateMovement's coarse corrections
 * for actors whose motion is server-driven but visually sensitive.
 *
 * Authority side:
 *   - On the SERVER this component samples the owner's world transform at NetUpdateFrequency cadence into
 *     the replicated Snapshot (location + quantized rotation), waking dormancy on change.
 *   - It DISABLES the owner's bReplicateMovement so the two systems don't fight (the standard guidance
 *     when you take over proxy smoothing).
 *
 * Simulated side:
 *   - OnRep_Snapshot pushes each arriving snapshot into the local history buffer.
 *   - The component ticks and writes the interpolated transform back onto the owner's root, so animation /
 *     gameplay reading the actor transform see smooth motion.
 *
 * The owning client / authority does NOT interpolate (it has the true transform already), so this only
 * acts on simulated proxies — checked every tick.
 */
UCLASS(ClassGroup = (DesignPatterns), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSNET_API UNet_InterpolatedTransformComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNet_InterpolatedTransformComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent

	/** Force-clear the interpolation buffer (call after a teleport so the proxy snaps, not glides). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Net|Interp")
	void ResetInterpolation();

	/**
	 * Visual interpolation delay in seconds. The proxy is rendered this far in the past so there is always a
	 * future sample to interpolate toward, hiding network jitter. Larger = smoother but laggier visuals.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Net|Interp", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float InterpDelay = 0.1f;

	/**
	 * Maximum seconds to dead-reckon (extrapolate) past the newest sample before freezing, used to glide
	 * across a dropped/late packet instead of snapping. Keep small to bound visible error on a long gap.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Net|Interp", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MaxExtrapolation = 0.25f;

	/** Server snapshot cadence (Hz). Higher = smoother proxies, more bandwidth. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Net|Interp", meta = (ClampMin = "1.0", ClampMax = "120.0"))
	float SnapshotFrequencyHz = 30.f;

	/** Local history ring capacity (samples). A few hundred ms at the snapshot rate is plenty. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Net|Interp", meta = (ClampMin = "2", ClampMax = "256"))
	int32 BufferCapacity = 32;

private:
	/** True if this component should be smoothing (owner is a simulated proxy on this machine). */
	bool ShouldInterpolate() const;

	/** Server: sample the owner transform into the replicated snapshot if the cadence elapsed. */
	void ServerCaptureSnapshot();

	/** OnRep: push the just-received snapshot into the local interpolation buffer. */
	UFUNCTION()
	void OnRep_Snapshot();

	/** The latest authoritative transform, replicated (NOT a history — one sample). */
	UPROPERTY(ReplicatedUsing = OnRep_Snapshot)
	FNet_TransformSnapshot Snapshot;

	/** Local, per-machine interpolation buffer rebuilt from observed snapshots (never replicated). */
	FNet_TransformHistory History;

	/** Server accumulator for the snapshot cadence. */
	float TimeSinceLastSnapshot = 0.f;

	/** Whether we already disabled the owner's bReplicateMovement on the server (do it once). */
	bool bDisabledOwnerMovementReplication = false;
};
