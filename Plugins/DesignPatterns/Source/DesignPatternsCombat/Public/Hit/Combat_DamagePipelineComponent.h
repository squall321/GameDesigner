// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Pipeline/Combat_DamageContext.h"
#include "Hit/Combat_HitTypes.h"
#include "Combat_DamagePipelineComponent.generated.h"

class UCombat_HitboxComponent;
class UCombat_PipelineDamageExecution;
class UCombat_HealthComponent;
class UCombat_StatusEffectComponent;
class UCombat_StatusStackController;
class UCombat_StatusEffect;
class UCombat_DefenseComponent;
class UCombat_PoiseComponent;

/** Fired (server) after a hit is fully resolved into a result, so local gameplay can react. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCombat_OnDamageResolved,
	UCombat_DamagePipelineComponent*, Component, const FCombat_DamageResult&, Result);

/**
 * THE AUTHORITY-SIDE SIDE-EFFECTS OWNER for the deep damage pipeline.
 *
 * The shipped UCombat_DamageExecution path (and the new pure UCombat_PipelineDamageExecution) compute
 * mitigated numbers WITHOUT side effects. This component is where all the consequences happen, on the
 * AUTHORITY only. In BeginPlay (authority) it binds every sibling UCombat_HitboxComponent's
 * OnHitConfirmed. When a hit confirms, HandleHitConfirmed (guarded at the TOP):
 *   1. Re-runs the PURE modifier + mitigation pass (via the hitbox's execution if it is a pipeline
 *      execution, else a fallback execution) to produce a final FCombat_DamageContext.
 *   2. Derives an immutable FCombat_DamageResult and CACHES it (GetLastResult) for the cosmetic
 *      hit-reaction component.
 *   3. Applies the instant HP damage through the victim's UCombat_HealthComponent.
 *   4. Applies the DoT portion via the victim's UCombat_StatusEffectComponent / stack controller.
 *   5. Applies poise damage to the victim's UCombat_PoiseComponent.
 *   6. Drains the victim's guard / consumes a parry on its UCombat_DefenseComponent.
 *   7. Applies a knockback impulse to the victim.
 *   8. Notifies the victim's ISeam_DamageReactor implementers (AI threat, audio, anim).
 *   9. Broadcasts a local cosmetic hit-feedback bus message.
 *
 * NOTE: this component lives on the ATTACKER (alongside the hitboxes). All the victim-side state it
 * mutates is reached through the victim actor's components, and each of those mutators re-guards
 * authority at its own top — defense-in-depth.
 */
UCLASS(ClassGroup = (DesignPatternsCombat), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSCOMBAT_API UCombat_DamagePipelineComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCombat_DamagePipelineComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent

	/** @return the most recently resolved result (valid only after a hit; check Victim validity). */
	UFUNCTION(BlueprintPure, Category = "DesignPatternsCombat|Pipeline")
	const FCombat_DamageResult& GetLastResult() const { return LastResult; }

	/** Broadcast (server) after each hit is resolved. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatternsCombat|Pipeline")
	FCombat_OnDamageResolved OnDamageResolved;

	// ---- Config (content-authored) ----

	/**
	 * Optional fallback execution used when a hitbox has no pipeline execution assigned. If null, the
	 * raw hit's mitigated-by-base damage is used. Instanced so it owns its modifier list.
	 */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "DesignPatternsCombat|Pipeline")
	TObjectPtr<UCombat_PipelineDamageExecution> FallbackExecution;

	/**
	 * Status effect class applied for the DoT portion of a converted hit. Designers pick a DoT effect
	 * (e.g. a burning effect) whose Magnitude the pipeline seeds from the DoT damage. Null disables DoT.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatternsCombat|Pipeline")
	TSubclassOf<UCombat_StatusEffect> DotEffectClass;

	/** Strength of the knockback impulse applied per point of final damage (cm/s per damage). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatternsCombat|Pipeline", meta = (ClampMin = "0.0"))
	float KnockbackPerDamage = 4.f;

	/** Extra upward impulse fraction added to knockback so hits pop targets slightly. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatternsCombat|Pipeline", meta = (ClampMin = "0.0"))
	float KnockbackUpFraction = 0.25f;

	/** Guard meter cost charged to a blocking victim per point of pre-block damage. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatternsCombat|Pipeline", meta = (ClampMin = "0.0"))
	float GuardCostPerDamage = 1.f;

protected:
	/** Bind to every sibling hitbox's OnHitConfirmed. AUTHORITY ONLY. */
	void BindHitboxes();

	/**
	 * Handle a confirmed hit (AUTHORITY ONLY — guarded at the TOP). Re-runs the pure pass, derives the
	 * result, and applies all side effects. Bound to UCombat_HitboxComponent::OnHitConfirmed.
	 */
	UFUNCTION()
	void HandleHitConfirmed(const FCombat_HitResult& Hit);

private:
	/** The cached latest resolved result, read by the hit-reaction component. */
	UPROPERTY(Transient)
	FCombat_DamageResult LastResult;

	/** The hitboxes we bound, so EndPlay can unbind cleanly. Non-owning weak refs. */
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<UCombat_HitboxComponent>> BoundHitboxes;

	/** Resolve the execution to run for a given hitbox (its pipeline execution, else the fallback). */
	UCombat_PipelineDamageExecution* ResolveExecution(UCombat_HitboxComponent* Hitbox) const;

	// ---- Side-effect appliers (each reached through the VICTIM's components) ----

	void ApplyInstantDamage(AActor* Victim, const FCombat_DamageResult& Result) const;
	void ApplyDot(AActor* Victim, const FCombat_DamageResult& Result) const;
	void ApplyPoise(AActor* Victim, FCombat_DamageContext& Context) const;
	void ApplyDefenseConsumption(AActor* Victim, const FCombat_DamageContext& Context) const;
	void ApplyKnockback(AActor* Victim, const FCombat_DamageResult& Result) const;
	void NotifyReactors(AActor* Victim, const FCombat_DamageResult& Result) const;
	void BroadcastHitFeedback(const FCombat_DamageResult& Result) const;

	/** Guard helper: true only if we own an actor and that actor has network authority. */
	bool HasAuthoritySafe() const;
};
