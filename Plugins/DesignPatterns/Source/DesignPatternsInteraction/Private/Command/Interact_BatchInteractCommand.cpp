// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Command/Interact_BatchInteractCommand.h"
#include "Component/Interact_InteractorComponent.h"
#include "DesignPatternsInteractionModule.h"
#include "Core/DPLog.h"

#include "GameFramework/Actor.h"

UInteract_BatchInteractCommand::UInteract_BatchInteractCommand()
{
	// A batch is a world-state change recorded for replay; not generally reversible.
	SetCommandKind(EDP_CommandKind::ReplayOnly);
	SetTag(InteractNativeTags::Cmd_BatchInteract);
}

UInteract_InteractorComponent* UInteract_BatchInteractCommand::ResolveInteractor(const FDP_CommandContext& Context) const
{
	if (AActor* Instigator = Context.ResolveInstigator<AActor>())
	{
		return Instigator->FindComponentByClass<UInteract_InteractorComponent>();
	}
	return nullptr;
}

bool UInteract_BatchInteractCommand::CanExecute_Implementation(const FDP_CommandContext& Context) const
{
	if (!Verb.IsValid())
	{
		return false;
	}
	return ResolveInteractor(Context) != nullptr;
}

bool UInteract_BatchInteractCommand::Execute_Implementation(const FDP_CommandContext& Context)
{
	UInteract_InteractorComponent* Interactor = ResolveInteractor(Context);
	if (!Interactor)
	{
		UE_LOG(LogDP, Warning, TEXT("[Interact] BatchInteractCommand: no interactor component on instigator."));
		return false;
	}

	// The interactor re-derives + clamps to its own server-side MaxBatchTargets (the authority cap).
	// RequestedMaxTargets is advisory; the interactor never exceeds its configured cap, so a command
	// cannot widen the batch beyond what the pawn is configured to allow.
	const int32 SuccessCount = Interactor->ServerBatchInteractAuthoritative(Verb);

	UE_LOG(LogDP, Verbose, TEXT("[Interact] BatchInteractCommand executed verb=%s success=%d"),
		*Verb.ToString(), SuccessCount);

	return SuccessCount > 0;
}
