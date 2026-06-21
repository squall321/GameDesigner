// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "Skill/Skill_Types.h"
#include "Skill_SkillTreeDefinition.generated.h"

class USkill_SkillDefinition;

/**
 * Authored definition of a whole skill tree / talent graph: the set of nodes that compose it plus the
 * tree-wide respec policy. Identity is the inherited DataTag (a Skill.* tag); the runtime progression
 * component is initialized from one of these and never hardcodes the node set or respec rules.
 *
 * Nodes are held as hard TObjectPtr references because a tree owns its node set and a designer needs them
 * all loaded together to author/visualize the graph; the registry resolves the tree by tag and the nodes
 * come along. Cross-node edges are still by DataTag (on the nodes), so the asset graph stays robust.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSSKILLTREE_API USkill_SkillTreeDefinition : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	USkill_SkillTreeDefinition();

	/** The nodes that make up this tree. Order is authoring-only; runtime uses each node's DataTag/Tier. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Skill|Tree")
	TArray<TObjectPtr<USkill_SkillDefinition>> Nodes;

	/** Whether and how this tree permits refunding spent points. See ESkill_RespecPolicy. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Skill|Respec")
	ESkill_RespecPolicy RespecPolicy = ESkill_RespecPolicy::Free;

	/**
	 * Currency cost charged for a respec when RespecPolicy is Cost (debited through the wallet seam using
	 * each node's CostCurrency, or a project-chosen tree currency). Ignored for None/Free. Authored, never
	 * hardcoded; clamped non-negative.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Skill|Respec",
		meta = (ClampMin = "0", UIMin = "0", EditCondition = "RespecPolicy == ESkill_RespecPolicy::Cost", EditConditionHides))
	int32 RespecCost = 0;

	/** True if this tree allows respec at all (policy is not None). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Skill")
	bool AllowsRespec() const { return RespecPolicy != ESkill_RespecPolicy::None; }

	/** Resolve the node whose DataTag matches NodeTag (nullptr if absent). Linear; trees are small. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Skill")
	USkill_SkillDefinition* FindNode(FGameplayTag NodeTag) const;

	/** Count of valid (non-null) nodes. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Skill")
	int32 GetValidNodeCount() const;

#if WITH_EDITOR
	//~ Begin UObject (editor validation)
	/** Flags null nodes, duplicate node DataTags, and prerequisite edges that point outside this tree. */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
	//~ End UObject
#endif
};
