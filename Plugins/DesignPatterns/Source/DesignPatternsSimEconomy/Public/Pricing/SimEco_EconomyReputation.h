// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

class ISeam_Reputation;

/**
 * Decoupled accessor for the ISeam_Reputation seam from within the economy module.
 *
 * Reputation is owned by the Narrative reputation subsystem, which implements ISeam_Reputation and
 * self-registers under DP.Service.Narrative.Reputation. The economy reads standing through that seam
 * WITHOUT including the Narrative concrete type: resolve the service from the GameInstance locator
 * (by a name-keyed tag so the economy doesn't even depend on Narrative's tag constants) and Cast<> to
 * ISeam_Reputation.
 *
 * All queries FAIL CLOSED: if no provider is registered, or the provider reports it does not track the
 * subject, callers get "unknown" (HasReputation == false / TryGetReputation == false) — an absent
 * provider is never silently treated as neutral standing in a way that would grant a discount.
 */
struct DESIGNPATTERNSSIMECONOMY_API FSimEco_EconomyReputation
{
	/** Resolve the live ISeam_Reputation provider from the service locator, or nullptr. */
	static ISeam_Reputation* Resolve(const UObject* WorldContext);

	/**
	 * Try to read Subject's numeric reputation with FactionTag. Returns false (leaving OutValue
	 * untouched) when no provider is registered or the provider does not track Subject.
	 */
	static bool TryGetReputation(const UObject* WorldContext, const AActor* Subject,
		FGameplayTag FactionTag, float& OutValue);

	/** True if a provider is registered AND it tracks Subject. */
	static bool HasReputation(const UObject* WorldContext, const AActor* Subject);
};
