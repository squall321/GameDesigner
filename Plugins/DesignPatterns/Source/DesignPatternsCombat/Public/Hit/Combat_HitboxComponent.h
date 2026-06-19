// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Hit/Combat_HitTypes.h"
#include "Combat_HitboxComponent.generated.h"

class UCombat_DamageExecution;
class UCombat_HealthComponent;

/**
 * Fired (server-side) for each newly confirmed hit during an active sweep window.
 * @param Hit the confirmed hit (victim, instigator, impact, base damage, type).
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCombat_OnHitConfirmed, const FCombat_HitResult&, Hit);

/**
 * Sweep / overlap-based melee hit detection.
 *
 * Lifecycle: BeginHitWindow() opens an "activation"; while open, ticks perform a sphere sweep
 * from the owner and confirm hits against any actor that has a UCombat_HealthComponent. A
 * per-activation dedupe set guarantees each victim is hit at most once per swing. EndHitWindow()
 * closes the activation and clears the set.
 *
 * AUTHORITY: hit confirmation and damage application are SERVER-AUTHORITATIVE. On clients the
 * window flags may be set for local VFX gating, but ProcessHits / damage only run with authority
 * (guarded at the top of ConfirmHit). Damage is routed through the pluggable
 * UCombat_DamageExecution Strategy before being applied to the victim's health component.
 */
UCLASS(ClassGroup = (DesignPatternsCombat), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSCOMBAT_API UCombat_HitboxComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCombat_HitboxComponent();

	//~ Begin UActorComponent
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent

	/**
	 * Open a hit window (start of a swing). Clears the per-activation dedupe set so the same
	 * victims can be hit again on the next swing. Safe to call on any machine, but only the
	 * server confirms hits.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatternsCombat|Hitbox")
	void BeginHitWindow();

	/** Close the hit window (end of a swing). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatternsCombat|Hitbox")
	void EndHitWindow();

	/** @return true while a hit window is open. */
	UFUNCTION(BlueprintPure, Category = "DesignPatternsCombat|Hitbox")
	bool IsHitWindowActive() const { return bHitWindowActive; }

	/**
	 * Run one sweep immediately and confirm any new hits (server only). Used for single-frame
	 * "instant" attacks; the continuous path calls this each tick while the window is open.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatternsCombat|Hitbox")
	void PerformSweep();

	/** Broadcast (server-side) for each newly confirmed hit. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatternsCombat|Hitbox")
	FCombat_OnHitConfirmed OnHitConfirmed;

	// ---- Config ----

	/** Radius of the sphere sweep, in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatternsCombat|Hitbox", meta = (ClampMin = "0.0"))
	float SweepRadius = 60.f;

	/** Forward reach of the sweep from the owner's location, in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatternsCombat|Hitbox", meta = (ClampMin = "0.0"))
	float SweepReach = 120.f;

	/** Base damage carried on each hit before the damage execution runs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatternsCombat|Hitbox", meta = (ClampMin = "0.0"))
	float BaseDamage = 25.f;

	/** Damage classification applied to confirmed hits. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatternsCombat|Hitbox")
	ECombat_DamageType DamageType = ECombat_DamageType::Physical;

	/** Optional source tag attached to each hit (e.g. the attack identity). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatternsCombat|Hitbox")
	FGameplayTag SourceTag;

	/** Collision channel used for the sweep. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatternsCombat|Hitbox")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Pawn;

	/**
	 * Pluggable damage calculation (Strategy). Authored inline; if null the BaseDamage is used
	 * directly. Instanced so each hitbox owns its own configured execution object.
	 */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "DesignPatternsCombat|Hitbox")
	TObjectPtr<UCombat_DamageExecution> DamageExecution;

protected:
	/** True while a hit window is open. Not replicated (windows are short and driven by montages). */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "DesignPatternsCombat|Hitbox")
	bool bHitWindowActive = false;

	/**
	 * Confirm a single hit against the victim (SERVER ONLY): dedupe, build the FCombat_HitResult,
	 * run the damage execution, apply to the victim's health component, broadcast OnHitConfirmed.
	 */
	void ConfirmHit(AActor* Victim, const FVector& ImpactPoint, const FVector& ImpactNormal);

private:
	/**
	 * Actors already hit during the current activation. Non-owning weak refs; cleared on each
	 * BeginHitWindow. Stored as raw weak pointers (not UPROPERTY) because this is transient,
	 * server-only bookkeeping never seen by GC-traversal of replicated state.
	 */
	TSet<TWeakObjectPtr<AActor>> HitActorsThisActivation;

	/** Guard helper: true only if we own an actor and that actor has network authority. */
	bool HasAuthoritySafe() const;
};
