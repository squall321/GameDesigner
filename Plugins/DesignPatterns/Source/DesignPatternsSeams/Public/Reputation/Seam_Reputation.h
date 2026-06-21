// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Seam_Reputation.generated.h"

UINTERFACE(MinimalAPI)
class USeam_Reputation : public UInterface
{
	GENERATED_BODY()
};

/**
 * Reputation / standing seam, owned by the Narrative reputation subsystem. Read by dialogue gates, RPG
 * quest-stage gates, and the economy (merchant discounts, bank access) — all without depending on the
 * Narrative module. Raw C++ virtuals (no Blueprint) keep this a lightweight per-leaf read seam.
 *
 * HasReputation is explicit so that an absent provider is distinguishable from "neutral standing" — a
 * consumer that needs reputation can fail safe instead of silently treating everyone as neutral.
 */
class DESIGNPATTERNSSEAMS_API ISeam_Reputation
{
	GENERATED_BODY()

public:
	/** True if reputation is tracked for this actor at all (a real provider is present and knows it). */
	virtual bool HasReputation(const AActor* Subject) const = 0;

	/** The numeric reputation of Subject with the given faction (0 if unknown). */
	virtual float GetReputation(const AActor* Subject, FGameplayTag FactionTag) const = 0;

	/** The discrete standing tier (e.g. Reputation.Tier.Hostile/Neutral/Honored) for UI/gating. */
	virtual FGameplayTag GetReputationTier(const AActor* Subject, FGameplayTag FactionTag) const = 0;

	/** True if Subject meets at least MinStanding with FactionTag (a gate check). */
	virtual bool MeetsStanding(const AActor* Subject, FGameplayTag FactionTag, int32 MinStanding) const = 0;
};
