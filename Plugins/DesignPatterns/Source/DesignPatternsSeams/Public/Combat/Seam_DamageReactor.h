// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Seam_DamageReactor.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_DamageReactor : public UInterface
{
	GENERATED_BODY()
};

/**
 * Reaction seam fired by the combat damage pipeline after damage is resolved, so other systems (AI threat,
 * audio, VFX, quests, hit-reaction animation) can respond without depending on the Combat module. Carries
 * PRIMITIVES only (no FCombat_DamageResult / FSeam_NetValue) so the Seams module stays a leaf.
 */
class DESIGNPATTERNSSEAMS_API ISeam_DamageReactor
{
	GENERATED_BODY()

public:
	/**
	 * Called on the damaged actor's reactors after mitigation.
	 * @param Instigator    Who dealt the damage (may be null).
	 * @param Mitigated     The final damage applied after the pipeline.
	 * @param DamageTypeTag The damage type/channel.
	 * @param ReactionTag   A classification for the reaction (e.g. Combat.Reaction.Stagger/Blocked/Crit).
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Combat")
	void OnDamageResolved(AActor* Instigator, float Mitigated, FGameplayTag DamageTypeTag, FGameplayTag ReactionTag);

	/** Called when the damaged actor is defeated/killed. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Combat")
	void OnDefeated(AActor* Killer);
};
