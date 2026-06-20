// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Command/DPGameplayCommand.h"
#include "Command/DPCommandContext.h"
#include "GameplayTagContainer.h"
#include "Interact_InteractCommand.generated.h"

/**
 * Optional Command-pattern wrapper around "perform an interaction", for games that route
 * interactions through the core command history (undo/redo or deterministic replay).
 *
 * Identity is DP.Cmd.Interact (set in the ctor). The stable target uses the core
 * FDP_CommandTargetHandle so a recorded command survives the target actor being destroyed and
 * respawned (level reload, pool recycle, network re-creation) — Resolve() falls back to a durable
 * soft path. The verb to perform is carried as a tag.
 *
 * AUTHORITY: like the interactor component, the actual Begin/EndInteract are authority-only. This
 * command is intended to run on the server (or in single-player) where the history subsystem lives;
 * Execute re-resolves the target and instigator's interactor component and drives it server-side.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Interact Command"))
class DESIGNPATTERNSINTERACTION_API UInteract_InteractCommand : public UDP_GameplayCommand
{
	GENERATED_BODY()

public:
	UInteract_InteractCommand();

	/**
	 * The verb to perform. Mirrored into the command context's Params on Execute is not needed —
	 * we keep it on the command instance so a BP author can configure it directly.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interact|Command",
		meta = (Categories = "DP.Data.Interact.Verb"))
	FGameplayTag Verb;

	/**
	 * Optional explicit target handle. When unset, Execute derives the target from the instigator's
	 * current focus (via its interactor component). Set this for replay/undo so the exact original
	 * target is restored regardless of where the instigator is now looking.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interact|Command")
	FDP_CommandTargetHandle TargetHandle;

	//~ Begin UDP_GameplayCommand
	virtual bool CanExecute_Implementation(const FDP_CommandContext& Context) const override;
	virtual bool Execute_Implementation(const FDP_CommandContext& Context) override;
	virtual bool Undo_Implementation(const FDP_CommandContext& Context) override;
	//~ End UDP_GameplayCommand

private:
	/** Resolve the instigator actor's interactor component from the command context. */
	class UInteract_InteractorComponent* ResolveInteractor(const FDP_CommandContext& Context) const;
};
