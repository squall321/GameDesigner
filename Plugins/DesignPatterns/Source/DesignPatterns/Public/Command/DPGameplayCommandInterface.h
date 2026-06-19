// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Command/DPCommandContext.h"
#include "DPGameplayCommandInterface.generated.h"

/**
 * UINTERFACE shell for IDP_GameplayCommand. Not Blueprintable directly — the concrete
 * UDP_GameplayCommand base implements it via BlueprintNativeEvents so designers subclass that.
 */
UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UDP_GameplayCommand : public UInterface
{
	GENERATED_BODY()
};

/**
 * The Command pattern's core abstraction, shared by both command forms:
 *   - UObject commands (UDP_GameplayCommand subclasses): full Blueprint authoring, undo/redo.
 *   - lightweight C++ struct commands that implement this interface for hot, allocation-free paths.
 *
 * A command encapsulates a single intent ("move here", "fire", "buy item") together with the
 * stable context needed to execute it now AND to undo / replay it later. Implementations must be
 * pure with respect to their FDP_CommandContext: given the same context they must produce the
 * same effect, which is what makes deterministic replay possible.
 */
class DESIGNPATTERNS_API IDP_GameplayCommand
{
	GENERATED_BODY()

public:
	/**
	 * Perform the command's effect. Returns true on success; a false return tells the history
	 * subsystem NOT to push the command onto the undo ring (it had no effect to reverse).
	 */
	virtual bool ExecuteCommand(const FDP_CommandContext& Context) = 0;

	/**
	 * Reverse a previously-executed command. Only ever called when IsUndoable() is true and the
	 * command was successfully executed. Returns true if the world was restored.
	 */
	virtual bool UndoCommand(const FDP_CommandContext& Context) = 0;

	/**
	 * Cheap pre-check: may this command execute in the current context? Used to reject invalid
	 * intent before it mutates state or enters the history. Must have no side effects.
	 */
	virtual bool CanExecuteCommand(const FDP_CommandContext& Context) const = 0;

	/** True if UndoCommand is meaningful for this command (i.e. it captures enough to reverse). */
	virtual bool IsUndoable() const = 0;

	/** Identity/category tag (e.g. DP.Cmd.Move). Drives filtering, debug dumps and replay routing. */
	virtual FGameplayTag GetCommandTag() const = 0;
};
