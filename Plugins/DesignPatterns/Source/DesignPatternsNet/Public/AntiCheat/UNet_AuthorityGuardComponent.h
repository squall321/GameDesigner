// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RPC/UNet_AuthorityComponent.h"
#include "UNet_AuthorityGuardComponent.generated.h"

/**
 * Anti-cheat / authority-hardening component that EXTENDS the canonical UNet_AuthorityComponent additively.
 * It overrides the existing CanServerApplyAction designer hook to layer two server-side validators on top of
 * the base behaviour BEFORE delegating to Super:
 *
 *   1. RATE LIMITING (token bucket): each requesting actor may only spend so many requests per window per
 *      action tag. A flood (turbo-fire / macro / replayed RPC) is dropped server-side, with a telemetry bus
 *      event, so a client cannot out-issue the server's intended cadence.
 *   2. MOVEMENT BOUNDS / SPEED: the owner's position+speed are sanity-checked against a configured world
 *      bound and max speed, catching teleport/speed hacks. (This is a generic guard; precise per-move
 *      validation belongs in a predicted CMC, but this catches gross violations on any action request.)
 *
 * Everything runs ON THE SERVER inside the base's apply step (CanServerApplyAction is the server-side gate),
 * so it is purely additive: the public API of UNet_AuthorityComponent is unchanged, and a project using the
 * base component keeps working. The generic ValidateServerRequest() helper is also exposed so other
 * server-authoritative request paths (not just RequestAction) can reuse the same rate-limit + bounds checks.
 */
UCLASS(ClassGroup = (DesignPatterns), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSNET_API UNet_AuthorityGuardComponent : public UNet_AuthorityComponent
{
	GENERATED_BODY()

public:
	UNet_AuthorityGuardComponent();

	//~ Begin UNet_AuthorityComponent
	/** Layers rate-limit + movement-bounds checks, then delegates to Super (server-side). */
	virtual bool CanServerApplyAction_Implementation(FGameplayTag ActionTag) const override;
	//~ End UNet_AuthorityComponent

	/**
	 * Generic server-authoritative request validator other intent paths can call. Returns true if the
	 * request is within rate + movement limits. MUTATES the rate-limit token bucket (consumes a token), so
	 * it is non-const. Logs + buses a flag on rejection. AUTHORITY ONLY (returns false off authority).
	 *
	 * @param RequestTag A tag identifying the request kind (its own rate bucket).
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Net|AntiCheat")
	bool ValidateServerRequest(FGameplayTag RequestTag);

	// ---- Tuning (data-driven; no hardcoded gameplay numbers) ----

	/** Max requests allowed per RateWindowSeconds per request tag, before requests are dropped. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Net|AntiCheat", meta = (ClampMin = "1"))
	int32 MaxRequestsPerWindow = 10;

	/** The sliding window length (seconds) for the rate limiter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Net|AntiCheat", meta = (ClampMin = "0.05"))
	float RateWindowSeconds = 1.0f;

	/** Enable the movement-bounds / speed sanity check on action requests. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Net|AntiCheat")
	bool bEnforceMovementBounds = true;

	/**
	 * Maximum plausible owner speed (uu/s) before a request is rejected as a speed cheat. Generous default-
	 * style guard; set to the project's true max with margin. Ignored when bEnforceMovementBounds is false.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Net|AntiCheat", meta = (ClampMin = "0.0"))
	float MaxOwnerSpeed = 2000.f;

	/**
	 * Half-extent (uu) of the allowed world bound centred on the origin. An owner outside this cube is
	 * treated as teleported out of bounds. 0 disables the position check (only speed is checked).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Net|AntiCheat", meta = (ClampMin = "0.0"))
	float WorldBoundHalfExtent = 0.f;

private:
	/** One token-bucket entry per request tag. */
	struct FRateBucket
	{
		/** Window start time (server seconds). */
		double WindowStart = 0.0;
		/** Requests counted in the current window. */
		int32 Count = 0;
	};

	/**
	 * Per-request-tag rate buckets (server-only, not replicated). Mutable so the const designer hook
	 * CanServerApplyAction_Implementation can consume tokens — the buckets are transient anti-cheat
	 * bookkeeping, not logical object state.
	 */
	mutable TMap<FGameplayTag, FRateBucket> RateBuckets;

	/** Consume a token for RequestTag; returns false if over the limit this window. */
	bool ConsumeRateToken(const FGameplayTag& RequestTag) const;

	/** True if the owner's current position+speed are within the configured movement bounds. */
	bool IsOwnerMovementSane() const;

	/** Raise the anti-cheat telemetry bus event for a rejected request. */
	void FlagRejection(const FGameplayTag& RequestTag, const TCHAR* Reason) const;
};
