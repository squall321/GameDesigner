// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Hit/Combat_DamageExecution.h"
#include "Pipeline/Combat_DamageContext.h"
#include "Combat_PipelineDamageExecution.generated.h"

class UCombat_DamageModifier;

/**
 * Ordered, data-driven damage Strategy that extends the shipped UCombat_DamageExecution.
 *
 * It overrides the REAL const CalculateDamage_Implementation(const FCombat_HitResult&) const, and is
 * STRICTLY PURE: it builds a transient FCombat_DamageContext on the stack, runs the authored
 * Modifiers in priority order, runs RunPureMitigation (READ-ONLY queries against the victim's
 * Defense / Poise / Weakpoint components), and returns the final mitigated float. It performs NO
 * world side effects — no ApplyDamage, no status application, no meter drain. Those all happen in
 * the authority-side UCombat_DamagePipelineComponent, which re-runs this same pure pass via
 * BuildContextAndRun to derive a FCombat_DamageResult before applying effects.
 *
 * Designers OPT IN by assigning this (or a subclass) to the hitbox's existing DamageExecution
 * UPROPERTY — zero hitbox edits, classic Strategy substitution.
 */
UCLASS(meta = (DisplayName = "Pipeline Damage Execution"))
class DESIGNPATTERNSCOMBAT_API UCombat_PipelineDamageExecution : public UCombat_DamageExecution
{
	GENERATED_BODY()

public:
	/**
	 * The ordered modifier chain. Authored inline; sorted by Priority each run. Each entry is PURE.
	 * Instanced so each execution owns its configured modifier objects.
	 */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "DesignPatternsCombat|Pipeline")
	TArray<TObjectPtr<UCombat_DamageModifier>> Modifiers;

	/** If true, the pure mitigation pass consults the victim's Defense/Poise/Weakpoint components. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatternsCombat|Pipeline")
	bool bUseDefenseMitigation = true;

	//~ Begin UCombat_DamageExecution
	/** Pure: build context, run modifiers + mitigation, fold to a final float. NO side effects. */
	virtual float CalculateDamage_Implementation(const FCombat_HitResult& Hit) const override;
	//~ End UCombat_DamageExecution

	/**
	 * Build a context from Hit and run the FULL pure pass (modifiers + mitigation), leaving the final
	 * context in OutContext for the side-effect owner to inspect (poise damage, DoT fraction, flags).
	 * CONST — does not mutate the execution or the world.
	 */
	void BuildContextAndRun(const FCombat_HitResult& Hit, FCombat_DamageContext& OutContext) const;

protected:
	/** Run the authored modifiers over Context in priority order (channel-gated). CONST/pure. */
	void RunModifiers(FCombat_DamageContext& Context) const;

	/**
	 * Read-only mitigation: query the victim's weakpoint (multiplier + crit flag), defense
	 * (chip / parry / invuln) and i-frames, folding results into the running context. CONST/pure —
	 * it never drains the guard meter or applies poise; it only READS to compute the final number.
	 */
	void RunPureMitigation(FCombat_DamageContext& Context) const;
};
