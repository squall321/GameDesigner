// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Seam_TeamAffinity.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_TeamAffinity : public UInterface
{
	GENERATED_BODY()
};

/**
 * Team-affinity seam. Implemented by the game-mode team component/subsystem; read by combat (friendly-
 * fire), AI (target selection), HUD (ally/enemy coloring) and respawn — all without depending on the
 * GameMode module. Teams are tag-keyed, and the friendly/hostile relation is policy-driven (so free-for-
 * all, team, and ally-faction setups all work).
 */
class DESIGNPATTERNSSEAMS_API ISeam_TeamAffinity
{
	GENERATED_BODY()

public:
	/** The team tag of the given actor (empty if none / not on a team). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Team")
	FGameplayTag GetTeamTag(const AActor* Actor) const;

	/** True if two actors are friendly under the current team policy (false = hostile/neutral). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Team")
	bool AreFriendly(const AActor* A, const AActor* B) const;
};
