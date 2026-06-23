// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Highlight/Rep_HighlightRuleSet.h"
#include "Core/DPLog.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

const FRep_HighlightRule* URep_HighlightRuleSet::FindRuleForEvent(const FGameplayTag& EventTag) const
{
	if (!EventTag.IsValid())
	{
		return nullptr;
	}

	// Prefer an exact tag match; otherwise accept a rule whose trigger is a parent of the event tag,
	// so a rule keyed on "Rep.Event.Score" also catches "Rep.Event.Score.Headshot".
	const FRep_HighlightRule* ParentMatch = nullptr;
	for (const FRep_HighlightRule& Rule : Rules)
	{
		if (!Rule.IsUsable())
		{
			continue;
		}
		if (Rule.TriggerTag == EventTag)
		{
			return &Rule;
		}
		if (!ParentMatch && EventTag.MatchesTag(Rule.TriggerTag))
		{
			ParentMatch = &Rule;
		}
	}
	return ParentMatch;
}

#if WITH_EDITOR
EDataValidationResult URep_HighlightRuleSet::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (Rules.Num() == 0)
	{
		Context.AddWarning(NSLOCTEXT("Replay", "Highlight_NoRules",
			"Highlight rule-set has no rules; no highlights will ever be detected."));
	}

	for (int32 Index = 0; Index < Rules.Num(); ++Index)
	{
		const FRep_HighlightRule& Rule = Rules[Index];
		if (!Rule.TriggerTag.IsValid())
		{
			Context.AddError(FText::Format(
				NSLOCTEXT("Replay", "Highlight_NoTrigger", "Rule {0} has no TriggerTag."),
				FText::AsNumber(Index)));
			Result = EDataValidationResult::Invalid;
		}
		if (!Rule.KindTag.IsValid())
		{
			Context.AddError(FText::Format(
				NSLOCTEXT("Replay", "Highlight_NoKind", "Rule {0} has no KindTag."),
				FText::AsNumber(Index)));
			Result = EDataValidationResult::Invalid;
		}
		if (Rule.WindowSeconds <= 0.f || Rule.MinEventCount < 1)
		{
			Context.AddError(FText::Format(
				NSLOCTEXT("Replay", "Highlight_BadWindow",
					"Rule {0} has a non-positive window or MinEventCount < 1."),
				FText::AsNumber(Index)));
			Result = EDataValidationResult::Invalid;
		}
	}

	return Result;
}
#endif
