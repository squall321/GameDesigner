// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "AI_SpawnRegionProvider.generated.h"

/**
 * OPTIONAL read seam describing where, in the world, a given spawn-region tag should place actors.
 *
 * A game/level may register an implementor under DP.Service.AI.SpawnRegions (WeakObserved). The spawn
 * director resolves it through the locator and asks it for a point inside a region; when NO provider is
 * registered the director falls back to its own designer-authored fallback point list (so the module is
 * self-sufficient without a level integration). Named ILvl_* to match the level-integration seam family
 * even though it is declared in this module (no level module exists to host it in Wave-1 AI).
 */
UINTERFACE(MinimalAPI, BlueprintType, meta = (CannotImplementInterfaceInBlueprint))
class ULvl_SpawnRegionProvider : public UInterface
{
	GENERATED_BODY()
};

/** @see ULvl_SpawnRegionProvider */
class DESIGNPATTERNSAI_API ILvl_SpawnRegionProvider
{
	GENERATED_BODY()

public:
	/**
	 * @return true if this provider knows region RegionTag (so the director should defer to it rather
	 * than its own fallback list for that tag).
	 */
	virtual bool HasSpawnRegion(const FGameplayTag& RegionTag) const = 0;

	/**
	 * Choose a world-space spawn transform inside RegionTag.
	 *
	 * @param RegionTag the region to sample.
	 * @param Seed      a caller-supplied seed so the director can vary placement deterministically.
	 * @param OutTransform receives the chosen transform on success.
	 * @return true if a point was produced; false if RegionTag is unknown (the director then falls back).
	 */
	virtual bool GetSpawnTransform(const FGameplayTag& RegionTag, int32 Seed, FTransform& OutTransform) const = 0;
};
