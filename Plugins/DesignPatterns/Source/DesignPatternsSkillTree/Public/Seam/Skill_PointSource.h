// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Skill_PointSource.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USkill_PointSource : public UInterface
{
	GENERATED_BODY()
};

/**
 * Module-private READ seam that tells the skill system how many skill points an owner has EARNED over a
 * given channel and what level the owner is, WITHOUT this module depending on the progression backend
 * that produces them (XP/leveling, quest rewards, achievements, purchased points, etc.).
 *
 * The available-point pool the progression component spends from is derived as
 * (total earned on a channel) - (points already spent on that channel), so this seam supplies the
 * "earned" half while the component owns the "spent" half. GetOwnerLevel feeds level-gated nodes.
 *
 * A project ships an adapter implementing this against its leveling/reward source; the progression area
 * resolves it from the owning pawn or from the service locator under
 * SkillNativeTags::Service_Skill_PointSource. When nothing resolves, both reads return 0 — a documented
 * inert default that simply means "no points / level 1-equivalent gating", never a crash. Read-only, so
 * implementations may answer on any net role.
 */
class DESIGNPATTERNSSKILLTREE_API ISkill_PointSource
{
	GENERATED_BODY()

public:
	/**
	 * Total skill points the owner has earned (cumulative, lifetime) on the given Channel (a Skill.Channel.*
	 * tag). An invalid Channel means "the default/main channel". Returns 0 when unknown. The available pool
	 * is this minus what the progression component has already spent on the same channel.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Skill|Seam")
	int32 GetTotalEarnedPoints(FGameplayTag Channel) const;

	/**
	 * The owner's current level, used by level-gated skill nodes/tiers. Returns 0 (treated as "ungated")
	 * when the owner has no level concept. Read-only.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Skill|Seam")
	int32 GetOwnerLevel() const;
};
