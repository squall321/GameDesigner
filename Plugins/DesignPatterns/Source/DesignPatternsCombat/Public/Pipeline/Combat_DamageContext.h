// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Hit/Combat_HitTypes.h"
#include "Combat_DamageContext.generated.h"

/**
 * Transient, per-hit working state threaded through the ordered damage-modifier pass.
 *
 * IT IS A PLAIN USTRUCT, NOT A UOBJECT, ON PURPOSE: a hit can occur many times per frame on many
 * actors; allocating a UObject context per hit would thrash the GC. The context is built on the
 * stack with FromHit(), passed by reference to each UCombat_DamageModifier::Modify, and discarded.
 *
 * The pipeline is STRICTLY PURE: modifiers only read the seam-resolved inputs and write the running
 * arithmetic fields below. They MUST NOT touch world state (no ApplyDamage, no spawning, no
 * status-effect application). All side effects are deferred to UCombat_DamagePipelineComponent,
 * which re-derives a FCombat_DamageResult from the same context AFTER the pure pass.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSCOMBAT_API FCombat_DamageContext
{
	GENERATED_BODY()

	// ---- Immutable inputs (filled by FromHit / the pipeline before the pass) ----

	/** The server-confirmed hit that started this calculation (victim, instigator, impact, type, source). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatternsCombat|Pipeline")
	FCombat_HitResult Hit;

	/** Damage channel tag mapped from Hit.DamageType, so modifiers can match by tag query. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatternsCombat|Pipeline")
	FGameplayTag DamageChannel;

	/** Bone the hit landed on, if the impact resolved to a skeletal bone (NAME_None otherwise). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatternsCombat|Pipeline")
	FName HitBoneName = NAME_None;

	// ---- Running arithmetic (modifiers read & write these in priority order) ----

	/** The base damage carried into the pipeline (defaults to Hit.BaseDamage). Never mutated by modifiers. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatternsCombat|Pipeline")
	float BaseDamage = 0.f;

	/** Running additive bonus applied BEFORE multipliers (e.g. flat armor pen, flat add). */
	UPROPERTY(BlueprintReadWrite, Category = "DesignPatternsCombat|Pipeline")
	float FlatBonus = 0.f;

	/** Running multiplicative factor (1 = no change). Crit, weakpoint and percent mods fold in here. */
	UPROPERTY(BlueprintReadWrite, Category = "DesignPatternsCombat|Pipeline")
	float MultiplierFactor = 1.f;

	/** Running resistance fraction in [0,1] removed AFTER bonuses/multipliers (0 = none, 1 = immune). */
	UPROPERTY(BlueprintReadWrite, Category = "DesignPatternsCombat|Pipeline")
	float ResistFraction = 0.f;

	/** Fraction of the final damage that should be converted to a damage-over-time effect (0..1). */
	UPROPERTY(BlueprintReadWrite, Category = "DesignPatternsCombat|Pipeline")
	float DotFraction = 0.f;

	/** Poise damage this hit should deal if it lands (independent of HP damage). */
	UPROPERTY(BlueprintReadWrite, Category = "DesignPatternsCombat|Pipeline")
	float PoiseDamage = 0.f;

	// ---- Flags written by modifiers / mitigation (read by the side-effect owner) ----

	/** True once a crit roll/forced-crit succeeded. */
	UPROPERTY(BlueprintReadWrite, Category = "DesignPatternsCombat|Pipeline")
	bool bIsCritical = false;

	/** True if the hit resolved onto a weakpoint hitzone. */
	UPROPERTY(BlueprintReadWrite, Category = "DesignPatternsCombat|Pipeline")
	bool bIsWeakpoint = false;

	/** True if a guard absorbed/blocked this hit (only chip damage remains). */
	UPROPERTY(BlueprintReadWrite, Category = "DesignPatternsCombat|Pipeline")
	bool bWasBlocked = false;

	/** True if a parry caught this hit (attacker should stagger; victim takes no damage). */
	UPROPERTY(BlueprintReadWrite, Category = "DesignPatternsCombat|Pipeline")
	bool bWasParried = false;

	/** True if i-frames / a dodge made the victim invulnerable to this hit. */
	UPROPERTY(BlueprintReadWrite, Category = "DesignPatternsCombat|Pipeline")
	bool bWasInvulnerable = false;

	/** True if the running poise damage broke the victim's poise (computed by the side-effect owner). */
	UPROPERTY(BlueprintReadWrite, Category = "DesignPatternsCombat|Pipeline")
	bool bStaggered = false;

	FCombat_DamageContext() = default;

	/**
	 * Build a fresh context from a confirmed hit. BaseDamage seeds from Hit.BaseDamage, the running
	 * factors are identity, and DamageChannel is resolved from the hit's ECombat_DamageType.
	 */
	static FCombat_DamageContext FromHit(const FCombat_HitResult& InHit);

	/**
	 * Fold the running arithmetic into a single mitigated damage value:
	 *   final = max(0, (BaseDamage + FlatBonus) * MultiplierFactor * (1 - clamp(ResistFraction)))
	 * Returns 0 when invulnerable/parried. PURE — no side effects.
	 */
	float ComputeFinalDamage() const;
};

/**
 * Immutable result of a full damage calculation+mitigation pass. NOT replicated: the Health/Poise
 * deltas it caused already replicate via their components, so re-replicating this would be redundant
 * (and would smuggle non-net-safe data). The side-effect owner caches the latest one so the
 * cosmetic hit-reaction component can read what just happened.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSCOMBAT_API FCombat_DamageResult
{
	GENERATED_BODY()

	/** The actor that took the hit. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatternsCombat|Pipeline")
	TWeakObjectPtr<AActor> Victim;

	/** The actor credited with the hit. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatternsCombat|Pipeline")
	TWeakObjectPtr<AActor> Instigator;

	/** Final HP damage applied (after all mitigation; 0 if blocked-to-zero / dodged / parried). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatternsCombat|Pipeline")
	float FinalDamage = 0.f;

	/** Poise damage that was applied. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatternsCombat|Pipeline")
	float PoiseDamage = 0.f;

	/** Portion of FinalDamage routed to a DoT instead of instant HP loss. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatternsCombat|Pipeline")
	float DotDamage = 0.f;

	/** Damage channel that was resolved. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatternsCombat|Pipeline")
	FGameplayTag DamageChannel;

	/** The single classification tag fed to ISeam_DamageReactor (Crit/Weakpoint/Blocked/Stagger/Hit). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatternsCombat|Pipeline")
	FGameplayTag ReactionTag;

	/** Impact point of the hit, for VFX placement. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatternsCombat|Pipeline")
	FVector ImpactPoint = FVector::ZeroVector;

	/** Mirror of the context flags so the cosmetic layer can branch without re-running the pipeline. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatternsCombat|Pipeline")
	bool bIsCritical = false;

	UPROPERTY(BlueprintReadOnly, Category = "DesignPatternsCombat|Pipeline")
	bool bIsWeakpoint = false;

	UPROPERTY(BlueprintReadOnly, Category = "DesignPatternsCombat|Pipeline")
	bool bWasBlocked = false;

	UPROPERTY(BlueprintReadOnly, Category = "DesignPatternsCombat|Pipeline")
	bool bWasParried = false;

	UPROPERTY(BlueprintReadOnly, Category = "DesignPatternsCombat|Pipeline")
	bool bWasInvulnerable = false;

	UPROPERTY(BlueprintReadOnly, Category = "DesignPatternsCombat|Pipeline")
	bool bStaggered = false;

	FCombat_DamageResult() = default;

	/** Build a result snapshot from a finished context (does NOT apply anything; pure copy). */
	static FCombat_DamageResult FromContext(const FCombat_DamageContext& Ctx);
};
