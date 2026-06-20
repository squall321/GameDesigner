// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Command/Interact_InteractCommand.h"
#include "Component/Interact_InteractorComponent.h"
#include "Types/Interact_Types.h"
#include "DesignPatternsInteractionModule.h"
#include "Core/DPLog.h"

#include "GameFramework/Actor.h"

UInteract_InteractCommand::UInteract_InteractCommand()
{
	// Interactions are recorded for replay by default (they are world-state changes), but are not
	// generally reversible: undo is a best-effort cancel. Designers can flip this to Undoable.
	SetCommandKind(EDP_CommandKind::ReplayOnly);
	SetTag(InteractNativeTags::Cmd_Interact);
}

UInteract_InteractorComponent* UInteract_InteractCommand::ResolveInteractor(const FDP_CommandContext& Context) const
{
	// The instigator of the command owns the interactor component that drives the interaction.
	if (AActor* Instigator = Context.ResolveInstigator<AActor>())
	{
		return Instigator->FindComponentByClass<UInteract_InteractorComponent>();
	}
	return nullptr;
}

bool UInteract_InteractCommand::CanExecute_Implementation(const FDP_CommandContext& Context) const
{
	// Side-effect free pre-check: we need an instigator with an interactor component and a verb.
	if (!Verb.IsValid())
	{
		return false;
	}
	return ResolveInteractor(Context) != nullptr;
}

bool UInteract_InteractCommand::Execute_Implementation(const FDP_CommandContext& Context)
{
	UInteract_InteractorComponent* Interactor = ResolveInteractor(Context);
	if (!Interactor)
	{
		UE_LOG(LogDP, Warning, TEXT("[Interact] InteractCommand: no interactor component on instigator."));
		return false;
	}

	// Drive the server-side authoritative path directly (the command runs where authority + history
	// live). The interactor re-derives/validates the target itself, so the command does not need to
	// trust TargetHandle as the actual target — it is metadata for replay fidelity only.
	const EInteract_Result Result = Interactor->ServerBeginInteractAuthoritative(Verb);

	UE_LOG(LogDP, Verbose, TEXT("[Interact] InteractCommand executed verb=%s result=%s target=%s"),
		*Verb.ToString(), *UEnum::GetValueAsString(Result), *TargetHandle.ToDebugString());

	return Result == EInteract_Result::Success;
}

bool UInteract_InteractCommand::Undo_Implementation(const FDP_CommandContext& Context)
{
	// Best-effort reversal: cancel the interaction this command started, if it is still active.
	UInteract_InteractorComponent* Interactor = ResolveInteractor(Context);
	if (!Interactor)
	{
		return false;
	}

	if (!Interactor->IsInteracting())
	{
		// Nothing to reverse; the interaction already finished. Report no-op failure so the history
		// subsystem knows this command could not be undone.
		return false;
	}

	Interactor->ServerEndInteractAuthoritative(EInteract_EndReason::Cancelled);
	return true;
}
