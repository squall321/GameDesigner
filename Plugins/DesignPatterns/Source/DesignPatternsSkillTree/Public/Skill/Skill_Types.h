// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Skill_Types.generated.h"

/**
 * Policy controlling whether (and at what cost) an authored skill tree allows a player to refund
 * spent points and relearn. Authored on USkill_SkillTreeDefinition; consumed by the learn/respec
 * command flow in the progression area. Genre-neutral — a roguelike may pick Free, an MMO Cost.
 */
UENUM(BlueprintType)
enum class ESkill_RespecPolicy : uint8
{
	/** Respec is disabled entirely; once a rank is learned it is permanent. */
	None,

	/** Respec is allowed at no currency cost (points are simply returned to the pool). */
	Free,

	/** Respec is allowed but charges RespecCost of the tree's cost currency through the wallet seam. */
	Cost
};

/**
 * One prerequisite edge in the skill graph: a skill node that must already be learned to at least
 * MinRank before the dependent node can be learned. A node with no prerequisites is a root/entry node.
 *
 * Pure authored data; the progression area validates these against the live learned-rank map. Edges
 * reference other nodes by their stable DataTag, never by object pointer, so a tree can be re-authored
 * or partially cooked without dangling references.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSKILLTREE_API FSkill_Prerequisite
{
	GENERATED_BODY()

	/** DataTag of the prerequisite skill node (a Skill.Node.* tag). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Skill")
	FGameplayTag SkillTag;

	/**
	 * Minimum learned rank required in SkillTag. Defaults to 1 (the node must simply be learned). For a
	 * multi-rank prerequisite a designer raises this. Clamped non-negative; 0 means "no rank required",
	 * which makes the edge inert (useful for temporarily disabling a dependency without deleting it).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Skill", meta = (ClampMin = "0", UIMin = "1"))
	int32 MinRank = 1;

	FSkill_Prerequisite() = default;
	FSkill_Prerequisite(const FGameplayTag& InSkillTag, int32 InMinRank)
		: SkillTag(InSkillTag), MinRank(InMinRank) {}

	/** A prerequisite is meaningful only if it names a skill and demands at least one rank. */
	bool IsActive() const { return SkillTag.IsValid() && MinRank > 0; }
};

/**
 * The computed answer to "can this owner learn the next rank of this skill right now?", together with
 * a player-facing reason when it cannot. Returned by the progression area's pure evaluator and surfaced
 * by skill-tree UI to grey out / tooltip nodes. Carries no authority and mutates nothing.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSKILLTREE_API FSkill_LearnResult
{
	GENERATED_BODY()

	/** True when every gate (rank cap, prerequisites, mutex, points, currency, level) is satisfied. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Skill")
	bool bCanLearn = false;

	/**
	 * Localized explanation shown when bCanLearn is false (e.g. "Requires Fireball rank 2",
	 * "Not enough skill points", "Conflicts with Heavy Armor"). Empty when bCanLearn is true.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Skill")
	FText Reason;

	FSkill_LearnResult() = default;

	/** Construct an allowing result. */
	static FSkill_LearnResult Allow()
	{
		FSkill_LearnResult R;
		R.bCanLearn = true;
		return R;
	}

	/** Construct a denying result carrying a player-facing reason. */
	static FSkill_LearnResult Deny(const FText& InReason)
	{
		FSkill_LearnResult R;
		R.bCanLearn = false;
		R.Reason = InReason;
		return R;
	}
};
