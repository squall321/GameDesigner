// Copyright DesignPatterns plugin. All Rights Reserved.

#include "FSM/DPStateMachineDefinition.h"
#include "FSM/DPState.h"
#include "Core/DPLog.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "DP_StateMachineDefinition"

FPrimaryAssetId UDP_StateMachineDefinition::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("DP_StateMachineDefinition"), GetFName());
}

UDP_State* UDP_StateMachineDefinition::FindState(FGameplayTag Tag) const
{
	if (!Tag.IsValid())
	{
		return nullptr;
	}

	for (const TObjectPtr<UDP_State>& State : States)
	{
		if (State && State->StateTag == Tag)
		{
			return State;
		}
	}
	return nullptr;
}

bool UDP_StateMachineDefinition::HasState(FGameplayTag Tag) const
{
	return FindState(Tag) != nullptr;
}

UDP_State* UDP_StateMachineDefinition::GetInitialState() const
{
	return FindState(InitialStateTag);
}

#if WITH_EDITOR
EDataValidationResult UDP_StateMachineDefinition::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	TSet<FGameplayTag> SeenTags;
	for (const TObjectPtr<UDP_State>& State : States)
	{
		if (!State)
		{
			Context.AddError(LOCTEXT("NullState", "States array contains a null entry."));
			Result = EDataValidationResult::Invalid;
			continue;
		}

		if (!State->StateTag.IsValid())
		{
			Context.AddError(FText::Format(
				LOCTEXT("EmptyTag", "State '{0}' has an empty StateTag."),
				FText::FromString(State->GetName())));
			Result = EDataValidationResult::Invalid;
		}
		else if (SeenTags.Contains(State->StateTag))
		{
			Context.AddError(FText::Format(
				LOCTEXT("DupTag", "Duplicate StateTag '{0}'."),
				FText::FromString(State->StateTag.ToString())));
			Result = EDataValidationResult::Invalid;
		}
		else
		{
			SeenTags.Add(State->StateTag);
		}
	}

	// Dangling transitions: ToState must resolve to a known state tag.
	for (const TObjectPtr<UDP_State>& State : States)
	{
		if (!State)
		{
			continue;
		}
		for (const FDP_StateTransition& Transition : State->Transitions)
		{
			if (Transition.ToState.IsValid() && !SeenTags.Contains(Transition.ToState))
			{
				Context.AddError(FText::Format(
					LOCTEXT("DanglingTransition", "State '{0}' transitions to unknown state '{1}'."),
					FText::FromString(State->StateTag.ToString()),
					FText::FromString(Transition.ToState.ToString())));
				Result = EDataValidationResult::Invalid;
			}
		}
	}

	if (!InitialStateTag.IsValid())
	{
		Context.AddError(LOCTEXT("NoInitial", "InitialStateTag is not set."));
		Result = EDataValidationResult::Invalid;
	}
	else if (!SeenTags.Contains(InitialStateTag))
	{
		Context.AddError(FText::Format(
			LOCTEXT("BadInitial", "InitialStateTag '{0}' does not match any state."),
			FText::FromString(InitialStateTag.ToString())));
		Result = EDataValidationResult::Invalid;
	}

	return Result;
}
#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE
