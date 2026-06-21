// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "Containers/Ticker.h"
#include "Identity/Seam_EntityId.h"
#include "Net/Seam_NetValue.h"
#include "UNet_LagCompensationSubsystem.generated.h"

class UDP_ServiceLocatorSubsystem;

/**
 * One timestamped bounds sample for a registered target. Server-only, never replicated. Stored in a
 * per-target ring so the subsystem can reconstruct where an actor's hurt-volume WAS at a past time.
 */
struct FNet_RewindSample
{
	/** Server world-seconds when the sample was taken. */
	double TimeSeconds = 0.0;

	/** The target's world-space collision bounds at that time. */
	FBoxSphereBounds Bounds = FBoxSphereBounds(ForceInit);
};

/**
 * Per-target history: a weak handle to the seam-implementing component plus its bounds ring. Held by
 * value in the subsystem map, keyed by the target's stable entity id.
 */
struct FNet_RewindTrack
{
	/** Weak ref to the registered seam implementer (a UObject implementing ISeam_HitRewindTarget). */
	TWeakObjectPtr<UObject> Target;

	/** The owning actor (resolved from the component), used for friendly-fire / instigator checks. */
	TWeakObjectPtr<AActor> OwnerActor;

	/** Newest-last ring of bounds samples. */
	TArray<FNet_RewindSample> Samples;
};

/**
 * The result of a lag-compensated hit-rewind validation, returned to the caller (Combat) so it can log /
 * present without re-deriving anything.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSNET_API FNet_RewindResult
{
	GENERATED_BODY()

	/** True if the rewound ray intersected a valid (non-friendly) target's bounds at the rewound time. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Net|LagComp")
	bool bHit = false;

	/** The entity id of the confirmed target (invalid if no hit). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Net|LagComp")
	FSeam_EntityId TargetId;

	/** World-space impact point on the rewound bounds (the ray entry point). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Net|LagComp")
	FVector ImpactPoint = FVector::ZeroVector;

	/** The actual rewind time used (clamped server time), for diagnostics. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Net|LagComp")
	double RewindTimeSeconds = 0.0;
};

/**
 * Server-side lag-compensation (hit-rewind) subsystem — the mechanism that lets a shooting client's view
 * be authoritative on the server despite latency. It records, for every registered ISeam_HitRewindTarget,
 * a short rolling history of world-space collision bounds. When a client reports a hit at a past
 * timestamp, ConfirmHitAtTime rewinds every target's bounds to that moment, tests the shooter's ray, and
 * — on a confirmed, non-friendly hit — routes the confirmed damage into the target via its seam
 * (ApplyConfirmedHit). This is the standard "rewind the world to when the shooter fired" technique used by
 * competitive shooters, wrapped behind a seam so Combat never includes Net.
 *
 * Server-only & non-replicated: all history lives here on the authority; nothing crosses the wire. The
 * subsystem self-registers under DP.Service.Net.LagComp so Combat resolves it to register targets and
 * (rarely) to validate hits directly.
 *
 * Engine pieces wrapped: the manual transform-history ring replaces what UCharacterMovementComponent's
 * built-in server-side rewind does for characters, generalized to any bounded actor; FBoxSphereBounds /
 * ray-vs-box math come from Engine (no PhysicsCore dependency).
 */
UCLASS()
class DESIGNPATTERNSNET_API UNet_LagCompensationSubsystem : public UDP_WorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	//~ Begin UDP_WorldSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_WorldSubsystem

	/**
	 * UWorldSubsystem has no HasWorldAuthority(); declare our own. True on server / standalone / listen
	 * host (any net mode that is not a pure client). Recording and confirmation only run with authority.
	 */
	bool HasWorldAuthority() const;

	// ---- Registration (server-only; called by Combat's hit-rewind target component) ----

	/**
	 * Register a target whose bounds should be recorded for rewind. Target must implement
	 * ISeam_HitRewindTarget. Idempotent: re-registering the same id refreshes the handle. AUTHORITY ONLY.
	 */
	void RegisterTarget(UObject* Target);

	/** Stop recording a target (on EndPlay / death). AUTHORITY ONLY. Safe if not registered. */
	void UnregisterTarget(UObject* Target);

	/** Number of currently-tracked targets. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Net|LagComp")
	int32 GetTrackedTargetCount() const { return Tracks.Num(); }

	// ---- The rewind query (server-only) ----

	/**
	 * Rewind to ShooterTimestamp and test the segment [TraceStart, TraceEnd] against every tracked target's
	 * bounds AS THEY WERE at that time. On the closest confirmed, non-friendly hit, calls the target's
	 * ISeam_HitRewindTarget::ApplyConfirmedHit with Magnitude/DamageChannel. AUTHORITY ONLY.
	 *
	 * @param Instigator       The shooter (used for friendly-fire filtering via ISeam_TeamAffinity).
	 * @param ShooterTimestamp Server world-seconds the shooter perceived the target at (clamped to MaxRewindMs).
	 * @param TraceStart/End   The shooter's ray in world space (muzzle -> aim point).
	 * @param DamageChannel    Damage type/channel tag forwarded to the confirmed target.
	 * @param Magnitude        Confirmed damage magnitude (Type==Float) forwarded to the target.
	 * @param OutResult        Filled with the validation outcome (hit/target/impact/rewind time).
	 * @return true if a target was confirmed and ApplyConfirmedHit was invoked.
	 */
	bool ConfirmHitAtTime(AActor* Instigator, double ShooterTimestamp,
		const FVector& TraceStart, const FVector& TraceEnd,
		FGameplayTag DamageChannel, const FSeam_NetValue& Magnitude,
		FNet_RewindResult& OutResult);

	/**
	 * Convenience overload that validates ONLY (no ApplyConfirmedHit), so a caller can decide whether to
	 * commit. AUTHORITY ONLY.
	 */
	bool RewindAndValidate(AActor* Instigator, double ShooterTimestamp,
		const FVector& TraceStart, const FVector& TraceEnd, FNet_RewindResult& OutResult) const;

	/** Authoritative server clock used for stamping samples and clamping rewind. */
	double ServerTimeSeconds() const;

	// ---- Tuning (data-driven) ----

	/** How long (ms) of bounds history to retain per target. Bounds memory and max rewind depth. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "DesignPatterns|Net|LagComp", meta = (ClampMin = "50", ClampMax = "1000"))
	int32 HistoryWindowMs = 300;

	/** Maximum rewind a shooter may request (ms). A timestamp older than now-this is clamped (anti-cheat). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "DesignPatterns|Net|LagComp", meta = (ClampMin = "50", ClampMax = "1000"))
	int32 MaxRewindMs = 250;

	/** Capture cadence (Hz). Samples are recorded at most this often per target. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "DesignPatterns|Net|LagComp", meta = (ClampMin = "10", ClampMax = "120"))
	float CaptureFrequencyHz = 60.f;

protected:
	/** Tick callback that records a bounds sample for each tracked target (server-only, paused-aware). */
	void CaptureFrame();

private:
	/** Resolve the team-affinity seam (for friendly-fire filtering), or null. */
	TScriptInterface<class ISeam_TeamAffinity> ResolveTeamAffinity() const;

	/** Resolve the service locator (GameInstance-scoped), or null. */
	UDP_ServiceLocatorSubsystem* GetLocator() const;

	/** Self-register under DP.Service.Net.LagComp (WeakObserved). */
	void RegisterSelfAsService();

	/** Sample a track's bounds at RewindTime (interpolating the ring), into OutBounds. */
	static bool SampleBoundsAtTime(const FNet_RewindTrack& Track, double RewindTime, FBoxSphereBounds& OutBounds);

	/** Ray (segment) vs AABB+sphere test. Returns true and the entry point + distance on intersection. */
	static bool SegmentIntersectsBounds(const FVector& Start, const FVector& End, const FBoxSphereBounds& Bounds,
		FVector& OutEntryPoint, float& OutEntryDistance);

	/** Drop tracks whose target has been GC'd. */
	void PruneDeadTracks();

	/** Per-target history, keyed by stable entity id. Server-only, never replicated. */
	TMap<FSeam_EntityId, FNet_RewindTrack> Tracks;

	/** Server accumulator for the capture cadence. */
	float TimeSinceLastCapture = 0.f;

	/** Handle for the per-frame capture tick delegate. */
	FTSTicker::FDelegateHandle CaptureTickHandle;
};
