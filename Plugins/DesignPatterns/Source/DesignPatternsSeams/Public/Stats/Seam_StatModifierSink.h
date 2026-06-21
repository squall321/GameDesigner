// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Stats/Seam_StatMod.h"
#include "Seam_StatModifierSink.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_StatModifierSink : public UInterface
{
	GENERATED_BODY()
};

/**
 * Sink for attribute modifiers, implemented by a stats component. It exposes TWO distinct paths so that
 * the deep RPG/Combat/Movement systems can contribute modifiers correctly on both server and clients —
 * this distinction is what prevents the equipment/encumbrance/status desync:
 *
 *  - AddModifierBatch / RemoveModifiersFromSource — AUTHORITY-ONLY. For gameplay-GRANTED buffs (a spell
 *    applies a +str buff). The implementer guards authority; clients receive the effect via replication.
 *
 *  - SetDerivedModifierGroup — LOCAL-DERIVED, NO authority guard, runs on server AND clients. For
 *    modifiers that are recomputed deterministically from already-replicated state (equipped items'
 *    affixes, set bonuses, carried-weight encumbrance, active status effects). These must NOT funnel
 *    through the authority-only path or they would silently vanish on clients.
 *
 * Consumers resolve TScriptInterface<ISeam_StatModifierSink> off the owning actor and contribute by
 * SourceTag, so a source's whole modifier group is replaced/removed atomically.
 */
class DESIGNPATTERNSSEAMS_API ISeam_StatModifierSink
{
	GENERATED_BODY()

public:
	/** Grant a batch of modifiers under SourceTag. AUTHORITY ONLY (gameplay-granted buffs). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Stats")
	void AddModifierBatch(FGameplayTag SourceTag, const TArray<FSeam_StatMod>& Mods);

	/** Remove every modifier previously granted under SourceTag. AUTHORITY ONLY. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Stats")
	void RemoveModifiersFromSource(FGameplayTag SourceTag);

	/**
	 * Replace the LOCAL-DERIVED modifier group for SourceTag (recomputed from replicated state).
	 * Runs on server AND clients; no authority guard. Passing an empty array clears the group.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Stats")
	void SetDerivedModifierGroup(FGameplayTag SourceTag, const TArray<FSeam_StatMod>& Mods);
};
