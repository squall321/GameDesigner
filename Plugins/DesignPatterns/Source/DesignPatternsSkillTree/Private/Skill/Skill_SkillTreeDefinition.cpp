// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Skill/Skill_SkillTreeDefinition.h"
#include "Skill/Skill_SkillDefinition.h"
#include "Core/DPLog.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "SkillTreeDefinition"

USkill_SkillTreeDefinition::USkill_SkillTreeDefinition()
{
	RespecPolicy = ESkill_RespecPolicy::Free;
	RespecCost = 0;
}

USkill_SkillDefinition* USkill_SkillTreeDefinition::FindNode(FGameplayTag NodeTag) const
{
	if (!NodeTag.IsValid())
	{
		return nullptr;
	}

	for (const TObjectPtr<USkill_SkillDefinition>& Node : Nodes)
	{
		if (Node && Node->DataTag == NodeTag)
		{
			return Node;
		}
	}
	return nullptr;
}

int32 USkill_SkillTreeDefinition::GetValidNodeCount() const
{
	int32 Count = 0;
	for (const TObjectPtr<USkill_SkillDefinition>& Node : Nodes)
	{
		if (Node)
		{
			++Count;
		}
	}
	return Count;
}

#if WITH_EDITOR
EDataValidationResult USkill_SkillTreeDefinition::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	// Respec cost must be non-negative when the policy charges for it.
	if (RespecPolicy == ESkill_RespecPolicy::Cost && RespecCost < 0)
	{
		Context.AddError(FText::Format(
			LOCTEXT("BadRespecCost", "Skill tree '{0}' has policy Cost but a negative RespecCost ({1})."),
			FText::FromString(GetName()), FText::AsNumber(RespecCost)));
		Result = EDataValidationResult::Invalid;
	}

	// Collect the set of node identity tags in this tree, flagging nulls and duplicates.
	TSet<FGameplayTag> NodeTags;
	NodeTags.Reserve(Nodes.Num());

	for (int32 Index = 0; Index < Nodes.Num(); ++Index)
	{
		const USkill_SkillDefinition* Node = Nodes[Index];
		if (!Node)
		{
			Context.AddWarning(FText::Format(
				LOCTEXT("NullNode", "Skill tree '{0}' has a null node at index {1}."),
				FText::FromString(GetName()), FText::AsNumber(Index)));
			continue;
		}

		if (!Node->DataTag.IsValid())
		{
			Context.AddError(FText::Format(
				LOCTEXT("NodeNoTag", "Skill tree '{0}': node '{1}' has no DataTag."),
				FText::FromString(GetName()), FText::FromString(Node->GetName())));
			Result = EDataValidationResult::Invalid;
			continue;
		}

		bool bAlreadyPresent = false;
		NodeTags.Add(Node->DataTag, &bAlreadyPresent);
		if (bAlreadyPresent)
		{
			Context.AddError(FText::Format(
				LOCTEXT("DupNodeTag", "Skill tree '{0}' has duplicate node DataTag '{1}'."),
				FText::FromString(GetName()), FText::FromString(Node->DataTag.ToString())));
			Result = EDataValidationResult::Invalid;
		}
	}

	// Every prerequisite edge must reference a node that exists inside this same tree.
	for (const TObjectPtr<USkill_SkillDefinition>& Node : Nodes)
	{
		if (!Node)
		{
			continue;
		}

		TArray<FSkill_Prerequisite> ActivePrereqs;
		Node->GetActivePrerequisites(ActivePrereqs);
		for (const FSkill_Prerequisite& Prereq : ActivePrereqs)
		{
			if (!NodeTags.Contains(Prereq.SkillTag))
			{
				Context.AddError(FText::Format(
					LOCTEXT("DanglingPrereq", "Skill tree '{0}': node '{1}' requires '{2}', which is not in this tree."),
					FText::FromString(GetName()),
					FText::FromString(Node->DataTag.ToString()),
					FText::FromString(Prereq.SkillTag.ToString())));
				Result = EDataValidationResult::Invalid;
			}
		}
	}

	return Result;
}
#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE
