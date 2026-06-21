// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Identity/Seam_EntityId.h"
#include "Net/Seam_NetValue.h"
#include "Seam_HitRewindTarget.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_HitRewindTarget : public UInterface
{
	GENERATED_BODY()
};

/**
 * Server-side lag-compensation seam — the shared boundary between the Net module's hit-rewind
 * subsystem (the CALLER) and the Combat module's damageable actors (the IMPLEMENTER). It exists so
 * neither module includes the other's concrete headers: Net never includes Combat, Combat never
 * includes Net.
 *
 * Lifecycle / data flow:
 *   1. A damageable Combat actor registers a component implementing this seam with
 *      UNet_LagCompensationSubsystem (which stores its recent collision bounds keyed by entity id).
 *   2. When a shooter reports a hit at a past client timestamp, the subsystem rewinds the recorded
 *      bounds of every registered target to that moment, validates the ray/segment against them
 *      (and friendly-fire policy via ISeam_TeamAffinity), then — on the AUTHORITY only — calls
 *      ApplyConfirmedHit on the validated target.
 *   3. The Combat implementer reads the FSeam_NetValue magnitude (validating Type==Float) and routes
 *      it into its local UCombat_HealthComponent::ApplyDamage. Authority is re-asserted there.
 *
 * NOTE: this seam carries only net/save-safe primitives plus FSeam_NetValue and FBoxSphereBounds
 * (Engine type). It pulls in NO Combat or Net concrete types, keeping DesignPatternsSeams a leaf.
 */
class DESIGNPATTERNSSEAMS_API ISeam_HitRewindTarget
{
	GENERATED_BODY()

public:
	/**
	 * Stable identity used to key this target inside the lag-comp subsystem's per-entity history ring.
	 * MUST be stable for the lifetime of the registration (typically the owning actor's entity id).
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Net|Rewind")
	FSeam_EntityId GetRewindEntityId() const;

	/**
	 * Current world-space collision bounds (a coarse box+sphere) that the subsystem snapshots every
	 * tick. Hit-rewind validates the shooter's ray against the bounds recorded at the rewound time,
	 * so this should tightly wrap the actor's hurt volume. Output is in world space.
	 *
	 * @return true if OutBounds was filled (a valid, non-degenerate volume); false to skip recording.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Net|Rewind")
	bool GetRewindBounds(FBoxSphereBounds& OutBounds) const;

	/**
	 * Apply a server-confirmed, lag-compensated hit. Called ONLY by the lag-comp subsystem on the
	 * authority after the rewind+validation succeeds. The implementer validates Magnitude.Type==Float
	 * and routes the value into its local damage component; it re-asserts authority before mutating.
	 *
	 * @param Instigator    The shooter actor the server resolved (may be null for environmental hits).
	 * @param DamageChannel Damage type / channel tag the caller validated (gameplay-defined).
	 * @param Magnitude     The confirmed damage magnitude as a net-safe value (expected Type==Float).
	 * @param HitBoneName   The collision bone the ray intersected at the rewound time (NAME_None if N/A).
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Net|Rewind")
	void ApplyConfirmedHit(AActor* Instigator, FGameplayTag DamageChannel, FSeam_NetValue Magnitude, FName HitBoneName);
};
