// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Effect/Combat_StatusFamilyEffect.h"
#include "Stats/Seam_StatMod.h"
#include "Combat_StatusEffect_StatMod.generated.h"

/**
 * A status-effect leaf that contributes ATTRIBUTE MODIFIERS while active — the status-effect arm of
 * the shared stat-modifier seam.
 *
 * THE DUAL-PATH STAT RULE (critical): a status effect is LOCAL-DERIVED from already-replicated state
 * (the status component replicates ActiveEffectTags; both server and clients know the effect is on).
 * Therefore its modifiers MUST be contributed through ISeam_StatModifierSink::SetDerivedModifierGroup
 * (runs on server AND clients, NO authority guard) — NOT the authority-only AddModifierBatch. Using
 * the authority path here would make the modifier silently vanish on clients (the classic desync).
 *
 * On OnApply it pushes its authored modifier group under a per-effect SourceTag; on OnRemove it
 * clears that group (empty array). The sink is resolved off the owning actor via the seam interface,
 * so this never includes the RPG/Stats concrete component.
 */
UCLASS(Blueprintable, BlueprintType, EditInlineNew, DefaultToInstanced)
class DESIGNPATTERNSCOMBAT_API UCombat_StatusEffect_StatMod : public UCombat_StatusFamilyEffect
{
	GENERATED_BODY()

public:
	/**
	 * The modifier group this effect contributes while active. Authored content (no magic numbers).
	 * Each FSeam_StatMod's SourceTag may be left empty; the effect overrides it with its derived
	 * per-effect source key so the whole group is replaced/cleared atomically.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DesignPatternsCombat|Status|StatMod")
	TArray<FSeam_StatMod> Modifiers;

	/**
	 * Optional explicit source key for this effect's derived group. If unset, a key is derived from
	 * the effect's EffectTag (or a stable fallback) so distinct effects do not collide.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DesignPatternsCombat|Status|StatMod")
	FGameplayTag ModifierSourceTag;

	//~ Begin UCombat_StatusEffect
	virtual void OnApply_Implementation(AActor* Target) override;
	virtual void OnRemove_Implementation(AActor* Target, bool bExpiredNaturally) override;
	//~ End UCombat_StatusEffect

protected:
	/** Resolve the effective source key (ModifierSourceTag, else EffectTag, else a status fallback). */
	FGameplayTag ResolveSourceTag() const;

	/** Push (Apply) or clear (Remove) this effect's derived modifier group on the target's stat sink. */
	void ContributeDerivedModifiers(AActor* Target, bool bApply) const;
};
