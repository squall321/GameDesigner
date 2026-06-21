// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Combat_PoiseComponent.generated.h"

class UCombat_PoiseComponent;
class UDP_GameplayActionComponent;

/**
 * Fired (on every machine) when poise breaks (stagger) or recovers.
 * @param Component the poise component.
 * @param bBroken   true when poise just broke (staggered), false when it recovered.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCombat_OnPoiseStateChanged,
	UCombat_PoiseComponent*, Component, bool, bBroken);

/**
 * Networked poise / stagger meter.
 *
 * REPLICATION: Poise and bStaggered replicate (OnRep) so clients can drive hit-react/stagger anims
 * and UI. All MUTATORS are AUTHORITY-ONLY and guarded at the top — ApplyPoiseDamage is called only
 * from the authority-side UCombat_DamagePipelineComponent (never from the pure execution).
 *
 * Poise regenerates after a content-authored delay following the last poise hit. While the owner's
 * UDP_GameplayActionComponent carries the hyperarmor owned-tag, poise damage is absorbed without
 * breaking (the meter still drains but cannot cross zero into a stagger).
 *
 * The component contributes its normalized fullness to the shared need seam via the defense
 * component (which composes guard + poise), so brains/UI read it generically.
 */
UCLASS(ClassGroup = (DesignPatternsCombat), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSCOMBAT_API UCombat_PoiseComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCombat_PoiseComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent

	/**
	 * Apply poise damage. AUTHORITY ONLY. Reduces Poise; if it crosses zero (and the owner lacks the
	 * hyperarmor tag) the owner is staggered: bStaggered is set, the staggered owned-tag is added, and
	 * a recovery timer is started. Returns true if this call broke poise.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatternsCombat|Poise")
	bool ApplyPoiseDamage(float Amount);

	/** Force-recover poise to full and clear stagger. AUTHORITY ONLY. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatternsCombat|Poise")
	void ResetPoise();

	/** @return current poise. */
	UFUNCTION(BlueprintPure, Category = "DesignPatternsCombat|Poise")
	float GetPoise() const { return Poise; }

	/** @return Poise / MaxPoise in [0,1] (0 if MaxPoise non-positive). */
	UFUNCTION(BlueprintPure, Category = "DesignPatternsCombat|Poise")
	float GetPoiseNormalized() const { return MaxPoise > 0.f ? FMath::Clamp(Poise / MaxPoise, 0.f, 1.f) : 0.f; }

	/** @return true while staggered (poise broken, not yet recovered). */
	UFUNCTION(BlueprintPure, Category = "DesignPatternsCombat|Poise")
	bool IsStaggered() const { return bStaggered; }

	/** @return true if the owner currently has the hyperarmor owned-tag (absorbs poise breaks). */
	UFUNCTION(BlueprintPure, Category = "DesignPatternsCombat|Poise")
	bool HasHyperarmor() const;

	/** Broadcast when poise breaks or recovers (every machine, via OnRep on clients). */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatternsCombat|Poise")
	FCombat_OnPoiseStateChanged OnPoiseStateChanged;

	// ---- Config (content-authored, no magic numbers) ----

	/** Maximum poise. Poise starts here and regenerates back to it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatternsCombat|Poise", meta = (ClampMin = "1.0"))
	float MaxPoise = 100.f;

	/** Poise restored per second once regen begins. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatternsCombat|Poise", meta = (ClampMin = "0.0"))
	float RegenPerSecond = 25.f;

	/** Seconds of no poise damage required before regen resumes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatternsCombat|Poise", meta = (ClampMin = "0.0"))
	float RegenDelay = 1.5f;

	/** How long a stagger lasts before poise resets to full (authority recovery window). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatternsCombat|Poise", meta = (ClampMin = "0.0"))
	float StaggerRecoverySeconds = 1.0f;

protected:
	/** Current poise. Replicated so clients can drive stagger feedback. */
	UPROPERTY(ReplicatedUsing = OnRep_Poise, VisibleInstanceOnly, BlueprintReadOnly, Category = "DesignPatternsCombat|Poise")
	float Poise = 100.f;

	/** True while staggered. Replicated. */
	UPROPERTY(ReplicatedUsing = OnRep_Staggered, VisibleInstanceOnly, BlueprintReadOnly, Category = "DesignPatternsCombat|Poise")
	bool bStaggered = false;

	/** Client reaction to a replicated poise change (currently informational; reserved for UI hooks). */
	UFUNCTION()
	void OnRep_Poise(float OldPoise);

	/** Client reaction to a replicated stagger flag: fire OnPoiseStateChanged. */
	UFUNCTION()
	void OnRep_Staggered();

private:
	/** World time (authority) of the last poise damage, for the regen delay. */
	float LastPoiseDamageTime = 0.f;

	/** World time (authority) at which the current stagger ends. 0 = not staggered. */
	float StaggerEndTime = 0.f;

	/** Resolve the owner's action component (for hyperarmor / staggered owned-tags). May be null. */
	UDP_GameplayActionComponent* GetActionComponent() const;

	/** Authority regen + stagger-recovery step run each tick. */
	void TickAuthority(float DeltaTime);

	/** @return current world time in seconds, or 0 if no world. */
	float GetWorldTimeSeconds() const;

	/** Guard helper: true only if we own an actor and that actor has network authority. */
	bool HasAuthoritySafe() const;
};
