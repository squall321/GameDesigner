// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Net/Seam_HitRewindTarget.h"
#include "Identity/Seam_EntityId.h"
#include "Combat_HitRewindTargetComponent.generated.h"

class UCombat_HealthComponent;

/**
 * The COMBAT SIDE of the shared Net <-> Combat hit-rewind (lag-compensation) seam.
 *
 * It implements ISeam_HitRewindTarget so the Net module's UNet_LagCompensationSubsystem (the CALLER)
 * can snapshot this actor's collision bounds each tick and, when a shooter reports a past-timestamp
 * hit, rewind + validate against them and then call ApplyConfirmedHit on the AUTHORITY. Neither module
 * includes the other's concrete headers — the seam (in DesignPatternsSeams) is the only contract.
 *
 * REGISTRATION (decoupled): Combat cannot include the Net subsystem, so this component ANNOUNCES its
 * presence to the lag-comp subsystem via the core message bus on a Net-owned registration channel
 * (resolved by name at runtime, so no cross-module tag include). The Net subsystem listens on that
 * channel and stores a weak ISeam_HitRewindTarget. EndPlay announces deregistration. If the Net
 * module is absent, the announcement simply has no listener — a clean no-op.
 *
 * NO REPLICATED STATE: this is purely an authority-side damage sink + a read-only bounds provider.
 * ApplyConfirmedHit validates Magnitude.Type==Float, re-asserts authority, and routes the value into
 * the local UCombat_HealthComponent::ApplyDamage (which guards authority again — defense in depth).
 */
UCLASS(ClassGroup = (DesignPatternsCombat), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSCOMBAT_API UCombat_HitRewindTargetComponent : public UActorComponent, public ISeam_HitRewindTarget
{
	GENERATED_BODY()

public:
	UCombat_HitRewindTargetComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent

	//~ Begin ISeam_HitRewindTarget
	virtual FSeam_EntityId GetRewindEntityId_Implementation() const override;
	virtual bool GetRewindBounds_Implementation(FBoxSphereBounds& OutBounds) const override;
	virtual void ApplyConfirmedHit_Implementation(AActor* Instigator, FGameplayTag DamageChannel, FSeam_NetValue Magnitude, FName HitBoneName) override;
	//~ End ISeam_HitRewindTarget

	/**
	 * Padding (cm) added around the owner's collision bounds when snapshotting, to keep the rewind
	 * volume a touch generous (compensates for sub-tick movement). Content-authored.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatternsCombat|Rewind", meta = (ClampMin = "0.0"))
	float BoundsPadding = 8.f;

protected:
	/** Announce (register=true / deregister=false) to the lag-comp subsystem via the bus. */
	void AnnounceRegistration(bool bRegister);

	/** Resolve a stable entity id for this target (EntityIdentity seam if present, else a local id). */
	FSeam_EntityId ResolveEntityId() const;

private:
	/** Lazily-created stable id used when the owner has no EntityIdentity seam (defensive fallback). */
	UPROPERTY(Transient)
	FSeam_EntityId FallbackEntityId;

	/** Guard helper: true only if we own an actor and that actor has network authority. */
	bool HasAuthoritySafe() const;
};
