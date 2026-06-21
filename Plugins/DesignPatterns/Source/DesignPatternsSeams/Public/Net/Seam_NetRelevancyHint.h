// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Seam_NetRelevancyHint.generated.h"

/**
 * Coarse bandwidth importance tier an actor advertises to the relevancy tuner. Higher tiers get a
 * higher net update frequency and a larger cull distance; lower tiers update lazily / go dormant.
 * The numeric values are an ordered scale (Critical highest) — the tuner maps each to concrete
 * frequency/cull values pulled from data (never hardcoded here).
 */
UENUM(BlueprintType)
enum class ESeam_NetRelevancyTier : uint8
{
	/** Player pawns, projectiles, anything whose lag is immediately visible. Highest priority. */
	Critical    UMETA(DisplayName = "Critical"),
	/** Important AI, vehicles, interactive objects in active play. */
	High        UMETA(DisplayName = "High"),
	/** Ambient AI, pickups, world props that update occasionally. */
	Normal      UMETA(DisplayName = "Normal"),
	/** Distant / cosmetic / rarely-changing actors; aggressive dormancy. Lowest priority. */
	Low         UMETA(DisplayName = "Low"),
	/** Static once placed; replicate initial state then sit dormant indefinitely. */
	Dormant     UMETA(DisplayName = "Dormant")
};

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_NetRelevancyHint : public UInterface
{
	GENERATED_BODY()
};

/**
 * Optional advisory seam an actor implements to bias the Net relevancy/bandwidth tuner. The tuner
 * (UNet_RelevancyTunerComponent) resolves this off the actor via Implements<> and, on the AUTHORITY
 * only, applies the corresponding net-update-frequency tier, cull distance and dormancy policy using
 * the engine's SetNetUpdateFrequency / SetNetCullDistanceSquared / SetNetDormancy accessors.
 *
 * It is purely a HINT: a non-implementing actor falls back to the tuner's configured default tier,
 * and the tuner clamps every value into a safe data-driven range, so a malicious or buggy hint can
 * never widen relevancy beyond the project's caps. Server-authoritative throughout.
 */
class DESIGNPATTERNSSEAMS_API ISeam_NetRelevancyHint
{
	GENERATED_BODY()

public:
	/** The importance tier this actor wants. The tuner maps it to concrete frequency/cull values. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Net|Relevancy")
	ESeam_NetRelevancyTier GetRelevancyTier() const;

	/**
	 * True if this actor wants to participate in dormancy (idle actors stop replicating until woken).
	 * The tuner only ever puts an actor dormant when this returns true AND the tier permits it.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Net|Relevancy")
	bool WantsDormancyWhenIdle() const;

	/**
	 * Optional per-actor cull-distance bias in Unreal units. The tuner adds this to the tier's base
	 * cull distance then clamps into the project's allowed range. Return 0 for "use the tier default".
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Net|Relevancy")
	float GetCullDistanceBias() const;
};
