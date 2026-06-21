// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Ruleset/GM_RulesetDefinition.h"

#include "Ruleset/GM_Condition.h"
#include "DesignPatternsGameModeModule.h"
#include "Core/DPLog.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "GM_RulesetDefinition"

UGM_RulesetDefinition::UGM_RulesetDefinition()
{
	// No magic gameplay numbers: RoundCount/TimeLimit/team/respawn config all default to the field
	// initializers declared in the header (single round, no time limit, no teams, respawn on). A designer
	// authors the actual values per ruleset asset.
}

bool UGM_RulesetDefinition::AnyWinConditionMet(UObject* WorldContext) const
{
	// ANY win condition firing ends the match as a win. Null entries are skipped defensively so a blank
	// slot left in the array never crashes the authority eval.
	for (const TObjectPtr<UGM_Condition>& Condition : WinConditions)
	{
		if (Condition && Condition->Evaluate(WorldContext))
		{
			return true;
		}
	}
	return false;
}

bool UGM_RulesetDefinition::AnyLoseConditionMet(UObject* WorldContext) const
{
	for (const TObjectPtr<UGM_Condition>& Condition : LoseConditions)
	{
		if (Condition && Condition->Evaluate(WorldContext))
		{
			return true;
		}
	}
	return false;
}

bool UGM_RulesetDefinition::AllStartConditionsMet(UObject* WorldContext) const
{
	// ALL start conditions must hold; an empty list means "always ready" so the match can start at once.
	for (const TObjectPtr<UGM_Condition>& Condition : StartConditions)
	{
		// A null entry is treated as an unmet condition would be unhelpful (it would block forever), so we
		// skip nulls and require only the authored conditions to hold.
		if (Condition && !Condition->Evaluate(WorldContext))
		{
			return false;
		}
	}
	return true;
}

#if WITH_EDITOR
EDataValidationResult UGM_RulesetDefinition::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	// RoundCount is also ClampMin=1 in the property meta, but validate explicitly so a programmatic/import
	// path that bypasses the editor clamp is still flagged.
	if (RoundCount < 1)
	{
		Context.AddError(FText::Format(
			LOCTEXT("BadRoundCount", "RoundCount must be >= 1 (got {0})."), FText::AsNumber(RoundCount)));
		Result = EDataValidationResult::Invalid;
	}

	// Duplicate team tags would make two scoreboard buckets collide; warn so the designer notices.
	TSet<FGameplayTag> SeenTeams;
	for (const FGM_TeamConfig& Team : Teams)
	{
		if (!Team.TeamTag.IsValid())
		{
			Context.AddWarning(LOCTEXT("EmptyTeamTag", "A team entry has an empty TeamTag."));
			continue;
		}
		if (SeenTeams.Contains(Team.TeamTag))
		{
			Context.AddError(FText::Format(
				LOCTEXT("DupTeamTag", "Duplicate team tag '{0}'."), FText::FromString(Team.TeamTag.ToString())));
			Result = EDataValidationResult::Invalid;
		}
		SeenTeams.Add(Team.TeamTag);
	}

	// Null condition entries are skipped at runtime but indicate an unfinished asset; warn per list.
	auto WarnNulls = [&Context](const TArray<TObjectPtr<UGM_Condition>>& List, const TCHAR* ListName)
	{
		for (int32 Index = 0; Index < List.Num(); ++Index)
		{
			if (!List[Index])
			{
				Context.AddWarning(FText::Format(
					LOCTEXT("NullCondition", "Null condition at index {0} in {1}."),
					FText::AsNumber(Index), FText::FromString(ListName)));
			}
		}
	};
	WarnNulls(WinConditions, TEXT("WinConditions"));
	WarnNulls(LoseConditions, TEXT("LoseConditions"));
	WarnNulls(StartConditions, TEXT("StartConditions"));

	return Result;
}
#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE
