// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Skill/Skill_SkillDefinition.h"
#include "Core/DPLog.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "SkillDefinition"

USkill_SkillDefinition::USkill_SkillDefinition()
{
	// Sensible defaults: a single-rank, one-point, no-currency, tier-0 entry node. Designers override.
	PointCost = 1;
	MaxRank = 1;
	Tier = 0;
}

void USkill_SkillDefinition::GetActivePrerequisites(TArray<FSkill_Prerequisite>& OutPrereqs) const
{
	OutPrereqs.Reset();
	OutPrereqs.Reserve(Prerequisites.Num());
	for (const FSkill_Prerequisite& Prereq : Prerequisites)
	{
		if (Prereq.IsActive())
		{
			OutPrereqs.Add(Prereq);
		}
	}
}

#if WITH_EDITOR
EDataValidationResult USkill_SkillDefinition::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	// MaxRank must allow at least the first learn.
	if (MaxRank < 1)
	{
		Context.AddError(FText::Format(
			LOCTEXT("BadMaxRank", "Skill '{0}' has MaxRank {1}; it must be at least 1."),
			FText::FromString(GetName()), FText::AsNumber(MaxRank)));
		Result = EDataValidationResult::Invalid;
	}

	// Point cost cannot be negative.
	if (PointCost < 0)
	{
		Context.AddError(FText::Format(
			LOCTEXT("BadPointCost", "Skill '{0}' has a negative PointCost ({1})."),
			FText::FromString(GetName()), FText::AsNumber(PointCost)));
		Result = EDataValidationResult::Invalid;
	}

	// Prerequisite edges must be well-formed and must not reference this node's own DataTag.
	for (int32 Index = 0; Index < Prerequisites.Num(); ++Index)
	{
		const FSkill_Prerequisite& Prereq = Prerequisites[Index];

		if (!Prereq.SkillTag.IsValid())
		{
			Context.AddWarning(FText::Format(
				LOCTEXT("EmptyPrereq", "Skill '{0}' prerequisite [{1}] has no SkillTag and will be ignored."),
				FText::FromString(GetName()), FText::AsNumber(Index)));
			continue;
		}

		if (DataTag.IsValid() && Prereq.SkillTag == DataTag)
		{
			Context.AddError(FText::Format(
				LOCTEXT("SelfPrereq", "Skill '{0}' lists itself as a prerequisite."),
				FText::FromString(GetName())));
			Result = EDataValidationResult::Invalid;
		}

		if (Prereq.MinRank < 0)
		{
			Context.AddError(FText::Format(
				LOCTEXT("BadPrereqRank", "Skill '{0}' prerequisite [{1}] has a negative MinRank."),
				FText::FromString(GetName()), FText::AsNumber(Index)));
			Result = EDataValidationResult::Invalid;
		}
	}

	return Result;
}
#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE
