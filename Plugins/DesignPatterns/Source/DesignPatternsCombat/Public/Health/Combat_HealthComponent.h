// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Combat_HealthComponent.generated.h"

class UCombat_HealthComponent;

/**
 * Fired (on every machine) when Health decreases. Delta is negative for damage.
 * @param Component the component whose health changed.
 * @param NewHealth health after the change.
 * @param Delta     signed change (negative = damage, positive = heal).
 * @param Instigator actor credited with the change, if known (may be null).
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FCombat_OnDamaged,
	UCombat_HealthComponent*, Component, float, NewHealth, float, Delta, AActor*, Instigator);

/**
 * Fired (on every machine) the first time Health reaches zero.
 * @param Component the component that died.
 * @param Killer    actor credited with the kill, if known (may be null).
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCombat_OnDeath,
	UCombat_HealthComponent*, Component, AActor*, Killer);

/**
 * Networked health/damage component.
 *
 * REPLICATION CONTRACT (per the core's fixed binding contract):
 *  - SetIsReplicatedByDefault(true) in the ctor.
 *  - Health & MaxHealth are Replicated with OnRep_ handlers so clients react to changes.
 *  - ApplyDamage / Heal / Kill / Revive are AUTHORITY-ONLY: they early-out on clients.
 *  - GetLifetimeReplicatedProps registers each replicated property (calls Super::).
 *
 * On death the server flips bIsDead (replicated), and every machine broadcasts a
 * DP.Bus.Combat.Death message via the core message bus (resolved through
 * FDP_SubsystemStatics) so decoupled systems (UI, scoring, AI) can react locally.
 */
UCLASS(ClassGroup = (DesignPatternsCombat), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSCOMBAT_API UCombat_HealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCombat_HealthComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent

	// ---- Authority-only mutators ----

	/**
	 * Apply damage to this component. AUTHORITY ONLY (no-op on clients).
	 * Clamps Health to [0, MaxHealth], fires OnDamaged, and triggers death if it hits zero.
	 * @param DamageAmount positive magnitude of damage to apply.
	 * @param Instigator   actor to credit for the damage / eventual kill.
	 * @return the actual damage applied (after clamping).
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatternsCombat|Health")
	float ApplyDamage(float DamageAmount, AActor* Instigator = nullptr);

	/**
	 * Heal this component. AUTHORITY ONLY (no-op on clients). Cannot revive the dead;
	 * use Revive for that. Clamps to MaxHealth and fires OnDamaged with a positive delta.
	 * @param HealAmount positive magnitude to restore.
	 * @return the actual amount healed (after clamping).
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatternsCombat|Health")
	float Heal(float HealAmount);

	/** Immediately set Health to zero and trigger death. AUTHORITY ONLY. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatternsCombat|Health")
	void Kill(AActor* Killer = nullptr);

	/**
	 * Revive a dead component to the given health fraction of MaxHealth. AUTHORITY ONLY.
	 * @param HealthFraction clamped to (0,1]; defaults to full health.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatternsCombat|Health")
	void Revive(float HealthFraction = 1.f);

	/** Set MaxHealth (and optionally rescale current Health proportionally). AUTHORITY ONLY. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatternsCombat|Health")
	void SetMaxHealth(float NewMaxHealth, bool bRescaleCurrent = true);

	// ---- Queries (safe on any machine) ----

	/** @return current health. */
	UFUNCTION(BlueprintPure, Category = "DesignPatternsCombat|Health")
	float GetHealth() const { return Health; }

	/** @return maximum health. */
	UFUNCTION(BlueprintPure, Category = "DesignPatternsCombat|Health")
	float GetMaxHealth() const { return MaxHealth; }

	/** @return Health / MaxHealth in [0,1] (0 if MaxHealth is non-positive). */
	UFUNCTION(BlueprintPure, Category = "DesignPatternsCombat|Health")
	float GetHealthPercent() const { return MaxHealth > 0.f ? Health / MaxHealth : 0.f; }

	/** @return true once Health has reached zero (until revived). */
	UFUNCTION(BlueprintPure, Category = "DesignPatternsCombat|Health")
	bool IsDead() const { return bIsDead; }

	// ---- Delegates (broadcast on every machine) ----

	/** Broadcast when health changes (damage or heal). */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatternsCombat|Health")
	FCombat_OnDamaged OnDamaged;

	/** Broadcast the first time health reaches zero. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatternsCombat|Health")
	FCombat_OnDeath OnDeath;

	// ---- Config ----

	/** Starting / maximum health applied at BeginPlay (authority sets Health to this). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatternsCombat|Health", meta = (ClampMin = "1.0"))
	float DefaultMaxHealth = 100.f;

protected:
	/** Current health. Replicated; clients react via OnRep_Health. */
	UPROPERTY(ReplicatedUsing = OnRep_Health, VisibleInstanceOnly, BlueprintReadOnly, Category = "DesignPatternsCombat|Health")
	float Health = 100.f;

	/** Maximum health. Replicated; clients react via OnRep_MaxHealth. */
	UPROPERTY(ReplicatedUsing = OnRep_MaxHealth, VisibleInstanceOnly, BlueprintReadOnly, Category = "DesignPatternsCombat|Health")
	float MaxHealth = 100.f;

	/** True once dead. Replicated so late joiners / clients see the death state. */
	UPROPERTY(ReplicatedUsing = OnRep_IsDead, VisibleInstanceOnly, BlueprintReadOnly, Category = "DesignPatternsCombat|Health")
	bool bIsDead = false;

	/** Replicated reference to the killer, so clients can attribute the death message. */
	UPROPERTY(Replicated)
	TWeakObjectPtr<AActor> LastInstigator;

	/** Client reaction to a replicated Health change: derive delta and fire OnDamaged. */
	UFUNCTION()
	void OnRep_Health(float OldHealth);

	/** Client reaction to a replicated MaxHealth change. */
	UFUNCTION()
	void OnRep_MaxHealth(float OldMaxHealth);

	/** Client reaction to a replicated death flag: fire OnDeath + broadcast the bus message. */
	UFUNCTION()
	void OnRep_IsDead();

private:
	/**
	 * Shared death handling run on the machine that observed the transition to dead.
	 * Fires OnDeath and broadcasts DP.Bus.Combat.Death on the core message bus.
	 */
	void HandleDeath(AActor* Killer);

	/** Broadcast a DP.Bus.Combat.Death message via the core bus (resolved at call time). */
	void BroadcastDeathMessage(AActor* Killer);

	/** Guard helper: true only if we own an actor and that actor has network authority. */
	bool HasAuthoritySafe() const;
};
