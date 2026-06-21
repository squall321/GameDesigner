// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Net/Seam_NetRelevancyHint.h"
#include "UNet_RelevancyLibrary.generated.h"

class AActor;

/**
 * Stateless helpers that wrap the engine's per-actor bandwidth knobs behind authority-guarded,
 * clamped, intent-revealing calls. These exist so gameplay code never pokes the engine's net fields
 * directly (which differs subtly across 5.3-5.5) and so every change is fail-closed off authority.
 *
 * ENGINE PIECES WRAPPED (and why this layer exists):
 *   - AActor::SetNetUpdateFrequency / SetNetCullDistanceSquared (UE5.5 accessors, replacing direct field
 *     writes to NetUpdateFrequency / NetCullDistanceSquared which are deprecated as public fields).
 *   - AActor::SetNetDormancy / FlushNetDormancy for dormancy management.
 * Each setter here checks HasAuthority first (relevancy/dormancy are server concepts) and clamps inputs to
 * the supplied caps, so a hint can never widen relevancy past the project's limits.
 */
UCLASS()
class DESIGNPATTERNSNET_API UNet_RelevancyLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Apply a relevancy tier's net-update frequency to Actor (authority-only, clamped). FrequencyHz is the
	 * resolved per-tier value; it is clamped into [MinHz, MaxHz] before being applied via the engine accessor.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Net|Relevancy")
	static void ApplyNetUpdateFrequency(AActor* Actor, float FrequencyHz, float MinHz = 1.f, float MaxHz = 120.f);

	/**
	 * Apply a net cull distance (in uu) to Actor (authority-only, clamped). The engine stores the SQUARE; this
	 * wrapper converts and clamps the linear distance into [MinDist, MaxDist] first.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Net|Relevancy")
	static void ApplyNetCullDistance(AActor* Actor, float DistanceUU, float MinDist = 100.f, float MaxDist = 200000.f);

	/**
	 * Set Actor's dormancy (authority-only). When bDormant is true the actor is put DORM_DormantAll (stops
	 * replicating until woken); when false it is woken to DORM_Awake and a net update is forced so pending
	 * deltas flush. No-op off authority.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Net|Relevancy")
	static void SetActorDormant(AActor* Actor, bool bDormant);

	/** Wake a dormant actor and force a net update so a just-changed delta replicates (authority-only). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Net|Relevancy")
	static void WakeDormantActor(AActor* Actor);

	/** Resolve an actor's relevancy hint (ISeam_NetRelevancyHint) if it implements one; else returns DefaultTier. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Net|Relevancy")
	static ESeam_NetRelevancyTier ResolveRelevancyTier(const AActor* Actor, ESeam_NetRelevancyTier DefaultTier = ESeam_NetRelevancyTier::Normal);
};
