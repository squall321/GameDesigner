// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Flow/Flow_FlowStateDefinition.h"
#include "Core/DPLog.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

bool UFlow_FlowStateDefinition::AllowsTransitionTo(FGameplayTag TargetPhase, bool bUndeclaredAllowed) const
{
	if (!TargetPhase.IsValid())
	{
		return false;
	}

	// An empty edge set means: open graph if the project allows undeclared transitions, otherwise
	// this is a terminal phase that permits no outgoing transition.
	if (AllowedTransitions.IsEmpty())
	{
		return bUndeclaredAllowed;
	}

	// Explicit edge set: allow if the target matches a declared edge (exact or child). A declared edge
	// of Flow.Phase.InGame thus also permits a game's Flow.Phase.InGame.Wave child.
	if (AllowedTransitions.HasTag(TargetPhase))
	{
		return true;
	}

	// Even with an explicit set, a project that allows undeclared transitions treats the set as a
	// "preferred" hint rather than a hard whitelist. The flow subsystem passes bUndeclaredAllowed=false
	// when it wants strict enforcement.
	return bUndeclaredAllowed;
}

#if WITH_EDITOR
EDataValidationResult UFlow_FlowStateDefinition::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	static const FName PhaseRootName(TEXT("Flow.Phase"));
	const FGameplayTag PhaseRoot = FGameplayTag::RequestGameplayTag(PhaseRootName, /*ErrorIfNotFound*/ false);

	if (!DataTag.IsValid())
	{
		Context.AddError(FText::FromString(TEXT(
			"Flow_FlowStateDefinition.DataTag is empty: it must be set to this phase's tag (e.g. Flow.Phase.MainMenu).")));
		Result = EDataValidationResult::Invalid;
	}
	else if (PhaseRoot.IsValid() && !DataTag.MatchesTag(PhaseRoot))
	{
		Context.AddWarning(FText::FromString(FString::Printf(TEXT(
			"Flow_FlowStateDefinition.DataTag '%s' is not under Flow.Phase; the flow subsystem resolves "
			"phase definitions by phase tag, so this asset will not be found for any phase."),
			*DataTag.ToString())));
	}

	return Result;
}
#endif
