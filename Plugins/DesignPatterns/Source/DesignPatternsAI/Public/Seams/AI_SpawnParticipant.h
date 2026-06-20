// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "AI_SpawnParticipant.generated.h"

/**
 * Implemented by anything the spawn director counts against its budget — typically the spawned enemy
 * actor (or a component on it). The director hands out budget when it spawns a participant and reclaims
 * that budget when IsAliveForBudget() flips false, so the live-budget accounting never depends on the
 * concrete enemy class. Implementors are usually game/genre actors; this module ships only the seam.
 *
 * The director holds participants as TWeakInterfacePtr and null-checks every access, so a participant
 * that is destroyed (and never told the director) simply drops out of the budget on the next reconcile.
 */
UINTERFACE(MinimalAPI, BlueprintType, meta = (CannotImplementInterfaceInBlueprint))
class UAI_SpawnParticipant : public UInterface
{
	GENERATED_BODY()
};

/** @see UAI_SpawnParticipant */
class DESIGNPATTERNSAI_API IAI_SpawnParticipant
{
	GENERATED_BODY()

public:
	/**
	 * @return how much of the encounter budget this participant occupies while alive (>= 0). A tougher
	 * enemy costs more so fewer of them fit under the same budget cap. The director reads this once at
	 * spawn (it must not change while the participant is alive).
	 */
	virtual int32 GetBudgetCost() const = 0;

	/**
	 * @return the wave tag this participant was spawned for. Lets the director attribute a kill to the
	 * right wave so it can fire DP.Bus.AI.Wave.Cleared when a wave's participants are all gone.
	 */
	virtual FGameplayTag GetWaveTag() const = 0;

	/**
	 * @return true while this participant should still occupy budget. False once it is dead, fully
	 * despawned, or returned to the pool. The director polls this to reclaim budget and detect wave
	 * clears; implementors should make it cheap (a flag read), not a search.
	 */
	virtual bool IsAliveForBudget() const = 0;
};
