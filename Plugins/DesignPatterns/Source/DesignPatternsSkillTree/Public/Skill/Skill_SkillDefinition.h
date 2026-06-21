// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "Skill/Skill_Types.h"
#include "Skill_SkillDefinition.generated.h"

/**
 * Authored definition of a single skill / talent node.
 *
 * A node is pure design-time content: its identity, its place in the dependency graph, what it costs to
 * learn, how many ranks it has, what ability it grants, and which passive modifier tags it confers. The
 * runtime progression component (sibling area) never hardcodes any of this — it reads it from the node.
 *
 * Identity is the inherited DataTag (a Skill.Node.* tag), so nodes are addressed by stable meaning and
 * resolved through the core data registry, not by asset path. No magic numbers live in code: cost, max
 * rank and tier are all authored here.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSSKILLTREE_API USkill_SkillDefinition : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	USkill_SkillDefinition();

	/**
	 * Skills that must already be learned (to their MinRank) before this node may be learned. Empty for a
	 * root/entry node. Each edge references another node by its DataTag, so the graph survives re-authoring.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Skill|Graph")
	TArray<FSkill_Prerequisite> Prerequisites;

	/**
	 * Point cost to learn ONE rank of this node, paid from the available point pool. The currency cost (if
	 * any) is separate (see CostCurrency). Authored, never hardcoded; clamped non-negative so a free node
	 * is expressible.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Skill|Cost", meta = (ClampMin = "0", UIMin = "0"))
	int32 PointCost = 1;

	/**
	 * OPTIONAL wallet currency tag charged (PointCost worth, or a per-rank amount the progression area
	 * derives) when learning this node, debited through the shared ISeam_Wallet/ISeam_PurchaseTarget
	 * seams. Leave unset for a node that costs only skill points. When set but no wallet seam resolves,
	 * the currency gate degrades to "free" (documented inert default) so single-player content still works.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Skill|Cost", meta = (Categories = "Economy.Currency"))
	FGameplayTag CostCurrency;

	/**
	 * Highest rank this node can reach. Rank 1 is the first learn; subsequent learns up to MaxRank are
	 * re-applications (each costs PointCost again). Clamped to at least 1. Most nodes are single-rank (1).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Skill|Rank", meta = (ClampMin = "1", UIMin = "1", UIMax = "10"))
	int32 MaxRank = 1;

	/**
	 * OPTIONAL ability identity tag (a Skill.Ability.* tag) granted through this module's
	 * ISkill_AbilityGranter seam when the node is learned, and revoked on respec. A project adapter maps
	 * this tag onto its ability backend (e.g. the core UDP_GameplayActionComponent). Leave unset for a
	 * purely passive node that only confers GrantedModifierTags.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Skill|Grant", meta = (Categories = "Skill.Ability"))
	FGameplayTag GrantedAbilityTag;

	/**
	 * OPTIONAL mutual-exclusion group (a Skill.Mutex.* tag). At most one node sharing a given MutexGroup
	 * may be learned at a time; the progression evaluator denies a node whose group already has a learned
	 * member. Leave unset for a node that has no conflicts.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Skill|Graph", meta = (Categories = "Skill.Mutex"))
	FGameplayTag MutexGroup;

	/**
	 * Passive modifier/identity tags this node confers while learned (e.g. damage-type unlocks, stat keys
	 * a consumer reads). Aggregated by the progression component into the owner's effective tag set and
	 * surfaced for other systems to query. Purely descriptive content — this module does not interpret them.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Skill|Grant")
	FGameplayTagContainer GrantedModifierTags;

	/**
	 * Layout/gating tier within the tree (column/depth). Used by UI for placement and by trees that gate a
	 * whole tier behind a minimum total spend. Authored; clamped non-negative. Tier 0 is the entry row.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Skill|Layout", meta = (ClampMin = "0", UIMin = "0", UIMax = "20"))
	int32 Tier = 0;

	/** True if learning this node grants a runtime ability (vs. a purely passive modifier node). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Skill")
	bool GrantsAbility() const { return GrantedAbilityTag.IsValid(); }

	/** True if this node participates in a mutual-exclusion group. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Skill")
	bool HasMutexGroup() const { return MutexGroup.IsValid(); }

	/** Only the active (valid + rank-demanding) prerequisite edges, skipping inert ones. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Skill")
	void GetActivePrerequisites(TArray<FSkill_Prerequisite>& OutPrereqs) const;

#if WITH_EDITOR
	//~ Begin UObject (editor validation)
	/** Validates rank/cost bounds and that prerequisite tags are well-formed and don't self-reference. */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
	//~ End UObject
#endif
};
