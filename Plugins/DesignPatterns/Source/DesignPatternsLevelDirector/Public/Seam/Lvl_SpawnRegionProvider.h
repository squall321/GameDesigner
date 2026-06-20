// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Lvl_SpawnRegionProvider.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class ULvl_SpawnRegionProvider : public UInterface
{
	GENERATED_BODY()
};

/**
 * "Spawn region provider" seam — anything that can hand out world-space spawn transforms.
 *
 * Implemented by ALvl_SpawnRegionVolume (a tagged AVolume) and by any game-authored provider that
 * wants to feed the AI spawn director. The AI spawn director (a SEPARATE module) resolves a
 * TScriptInterface<ILvl_SpawnRegionProvider> from the service locator and asks for filtered points;
 * it never hard-depends on this module's concrete volume type.
 *
 * The Filter tag is matched by the provider against each candidate point's team/role/faction tags.
 * An invalid (empty) Filter means "every point this provider offers". Providers must APPEND to
 * OutTransforms (never clear it) so a caller can accumulate across several providers.
 */
class DESIGNPATTERNSLEVELDIRECTOR_API ILvl_SpawnRegionProvider
{
	GENERATED_BODY()

public:
	/**
	 * Append the spawn transforms this provider offers that match Filter into OutTransforms.
	 *
	 * @param Filter         Tag to match against each point's filter tags. Empty/invalid = match all.
	 * @param OutTransforms  Caller-owned array to APPEND results to (must not be cleared by the impl).
	 *
	 * Implementations should return points already validated for being inside their region; the
	 * caller is responsible for any further collision/navmesh checks at spawn time.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|LevelDirector|Spawn")
	void GetSpawnPoints(FGameplayTag Filter, TArray<FTransform>& OutTransforms) const;
};
