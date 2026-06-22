// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Seam_FactionStanding.generated.h"

UINTERFACE(MinimalAPI)
class USeam_FactionStanding : public UInterface
{
	GENERATED_BODY()
};

/**
 * Faction-vs-faction standing seam (the matrix counterpart to actor-vs-faction ISeam_Reputation), owned by
 * the World hub's faction-matrix component. AI, economy and narrative read faction relationships through it
 * without depending on the World module. Raw C++ virtuals to match ISeam_Reputation's house style.
 *
 * HasFactionMatrix is explicit so an absent provider is distinguishable from "everyone neutral".
 */
class DESIGNPATTERNSSEAMS_API ISeam_FactionStanding
{
	GENERATED_BODY()

public:
	/** True if a faction-standing matrix is actually present. */
	virtual bool HasFactionMatrix() const = 0;

	/** Numeric standing of faction A toward faction B (0 when unknown / no matrix). */
	virtual float GetStanding(FGameplayTag FactionA, FGameplayTag FactionB) const = 0;

	/** Discrete standing tier (e.g. Faction.Tier.Hostile/Neutral/Allied) for A toward B. */
	virtual FGameplayTag GetStandingTier(FGameplayTag FactionA, FGameplayTag FactionB) const = 0;

	/** True if A is hostile toward B under the current matrix. */
	virtual bool AreHostile(FGameplayTag FactionA, FGameplayTag FactionB) const = 0;
};
