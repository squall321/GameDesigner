// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "GameplayTagQuery.h"
#include "Pipeline/Combat_DamageContext.h"
#include "Combat_DamageModifier.generated.h"

/**
 * One ordered, PURE step in the damage pipeline (Chain-of-Responsibility / Decorator hybrid).
 *
 * A modifier reads the seam-resolved inputs on the FCombat_DamageContext and mutates the running
 * arithmetic fields (FlatBonus / MultiplierFactor / ResistFraction / DotFraction / PoiseDamage) and
 * flags. It is STRICTLY PURE: no world side effects, no ApplyDamage, no spawning, no status
 * application — that all happens later in UCombat_DamagePipelineComponent. Purity is what lets the
 * pipeline run identically on the server (authoritative) and on clients (prediction) and inside the
 * const UCombat_PipelineDamageExecution::CalculateDamage_Implementation.
 *
 * Modifiers are EditInlineNew + Instanced so a designer authors a concrete ordered list directly on
 * the pipeline execution asset.
 */
UCLASS(Abstract, EditInlineNew, Blueprintable, BlueprintType, DefaultToInstanced)
class DESIGNPATTERNSCOMBAT_API UCombat_DamageModifier : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Lower runs first. Designers order armor/resistance, then flat adds, then multipliers, then crit.
	 * Stable sort by this value happens once in the execution before the pass.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatternsCombat|Pipeline")
	int32 Priority = 0;

	/**
	 * Optional gate: if set (non-empty), the modifier only runs when the context's DamageChannel
	 * matches this query. An empty query always matches (the common case).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatternsCombat|Pipeline")
	FGameplayTagQuery AppliesToDamageTypes;

	/**
	 * Apply this modifier to the running context. PURE — read inputs, write the arithmetic/flags.
	 * Default implementation does nothing.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "DesignPatternsCombat|Pipeline")
	void Modify(UPARAM(ref) FCombat_DamageContext& Context) const;
	virtual void Modify_Implementation(UPARAM(ref) FCombat_DamageContext& Context) const;

	/**
	 * @return true if this modifier should run for the context's damage channel (evaluates the query).
	 * Called by the execution before Modify; a modifier need not re-check the channel itself.
	 */
	bool MatchesChannel(const FCombat_DamageContext& Context) const;
};
