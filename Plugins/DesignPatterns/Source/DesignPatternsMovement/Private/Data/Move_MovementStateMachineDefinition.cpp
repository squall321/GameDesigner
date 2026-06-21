// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Data/Move_MovementStateMachineDefinition.h"
#include "State/Move_MovementState.h"
#include "Move_NativeTags.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

UMove_MovementStateMachineDefinition::UMove_MovementStateMachineDefinition()
{
	// Locomotion machines start grounded.
	InitialStateTag = MoveNativeTags::State_Walk;
}

#if WITH_EDITOR
EDataValidationResult UMove_MovementStateMachineDefinition::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	bool bHasWalk = false;
	for (const TObjectPtr<UDP_State>& State : States)
	{
		if (State == nullptr)
		{
			continue;
		}
		if (!State->IsA<UMove_MovementState>())
		{
			Context.AddError(FText::FromString(FString::Printf(
				TEXT("Movement definition state '%s' is not a UMove_MovementState."),
				*State->StateTag.ToString())));
			Result = EDataValidationResult::Invalid;
		}
		if (State->StateTag == MoveNativeTags::State_Walk)
		{
			bHasWalk = true;
		}
	}

	if (!bHasWalk)
	{
		Context.AddWarning(FText::FromString(
			TEXT("Movement definition has no Move.State.Walk state; the default initial state will fail to resolve.")));
	}

	return Result;
}
#endif
