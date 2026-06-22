// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Command/DPGameplayCommand.h"
#include "Command/DPCommandContext.h"
#include "GameplayTagContainer.h"
#include "Interact_BatchInteractCommand.generated.h"

class UInteract_InteractorComponent;

/**
 * Command-pattern wrapper around a multi-target "interact with all" (loot-all / harvest-all) for
 * games that route interactions through the core command history (replay/undo).
 *
 * Identity is DP.Cmd.BatchInteract. Like UInteract_InteractCommand it drives the SERVER-side
 * authoritative path: Execute re-resolves the instigator's interactor component and calls its
 * ServerBatchInteractAuthoritative, which re-derives ALL eligible candidates, clamps to the
 * interactor's MaxBatchTargets, runs BeginInteract on each accepted one, and broadcasts a single
 * DP.Bus.Interact.BatchComplete. Authority-only by contract.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Batch Interact Command"))
class DESIGNPATTERNSINTERACTION_API UInteract_BatchInteractCommand : public UDP_GameplayCommand
{
	GENERATED_BODY()

public:
	UInteract_BatchInteractCommand();

	/** The verb to apply to every eligible target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interact|Command",
		meta = (Categories = "DP.Data.Interact.Verb"))
	FGameplayTag Verb;

	/**
	 * Optional ceiling the command requests for this batch. The interactor STILL clamps to its own
	 * server-side MaxBatchTargets (the authority cap); this only lets a command request fewer. 0 =
	 * use the interactor's MaxBatchTargets unchanged.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interact|Command", meta = (ClampMin = "0"))
	int32 RequestedMaxTargets = 0;

	//~ Begin UDP_GameplayCommand
	virtual bool CanExecute_Implementation(const FDP_CommandContext& Context) const override;
	virtual bool Execute_Implementation(const FDP_CommandContext& Context) override;
	//~ End UDP_GameplayCommand

private:
	/** Resolve the instigator actor's interactor component from the command context. */
	UInteract_InteractorComponent* ResolveInteractor(const FDP_CommandContext& Context) const;
};
