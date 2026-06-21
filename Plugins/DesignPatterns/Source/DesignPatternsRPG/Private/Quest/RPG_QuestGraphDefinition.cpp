// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Quest/RPG_QuestGraphDefinition.h"
#include "Core/DPLog.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

const FRPG_QuestStage* URPG_QuestGraphDefinition::FindStage(FGameplayTag StageTag) const
{
	if (!StageTag.IsValid())
	{
		return nullptr;
	}
	for (const FRPG_QuestStage& Stage : Stages)
	{
		if (Stage.StageTag == StageTag)
		{
			return &Stage;
		}
	}
	return nullptr;
}

const FRPG_QuestStage* URPG_QuestGraphDefinition::GetStartStage() const
{
	return FindStage(StartStage);
}

#if WITH_EDITOR
EDataValidationResult URPG_QuestGraphDefinition::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	// Only validate the graph layer when this asset is actually branching.
	if (Stages.Num() == 0)
	{
		return Result;
	}

	if (!StartStage.IsValid())
	{
		Context.AddError(FText::FromString(TEXT("URPG_QuestGraphDefinition has stages but StartStage is unset.")));
		Result = EDataValidationResult::Invalid;
	}
	else if (FindStage(StartStage) == nullptr)
	{
		Context.AddError(FText::FromString(FString::Printf(
			TEXT("StartStage '%s' does not name any stage in Stages."), *StartStage.ToString())));
		Result = EDataValidationResult::Invalid;
	}

	// Unique stage tags + valid branch targets.
	TSet<FGameplayTag> SeenStages;
	for (const FRPG_QuestStage& Stage : Stages)
	{
		if (!Stage.StageTag.IsValid())
		{
			Context.AddError(FText::FromString(TEXT("A quest stage has an invalid (unset) StageTag.")));
			Result = EDataValidationResult::Invalid;
			continue;
		}
		bool bAlreadySeen = false;
		SeenStages.Add(Stage.StageTag, &bAlreadySeen);
		if (bAlreadySeen)
		{
			Context.AddError(FText::FromString(FString::Printf(
				TEXT("Duplicate stage tag '%s'."), *Stage.StageTag.ToString())));
			Result = EDataValidationResult::Invalid;
		}

		for (const FRPG_QuestBranchOutcome& Outcome : Stage.Outcomes)
		{
			// A branch that neither completes/fails nor names an existing next stage is a dead end.
			if (!Outcome.bCompletesQuest && !Outcome.bFailsQuest && Outcome.NextStage.IsValid()
				&& FindStage(Outcome.NextStage) == nullptr)
			{
				Context.AddError(FText::FromString(FString::Printf(
					TEXT("Stage '%s' branch NextStage '%s' does not exist."),
					*Stage.StageTag.ToString(), *Outcome.NextStage.ToString())));
				Result = EDataValidationResult::Invalid;
			}
		}

		if (Stage.FailToStage.IsValid() && FindStage(Stage.FailToStage) == nullptr)
		{
			Context.AddError(FText::FromString(FString::Printf(
				TEXT("Stage '%s' FailToStage '%s' does not exist."),
				*Stage.StageTag.ToString(), *Stage.FailToStage.ToString())));
			Result = EDataValidationResult::Invalid;
		}
	}

	return Result;
}
#endif // WITH_EDITOR
