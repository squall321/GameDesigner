// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Command/DPGameplayCommand.h"
#include "Core/DPLog.h"

UDP_GameplayCommand::UDP_GameplayCommand()
{
}

bool UDP_GameplayCommand::Execute_Implementation(const FDP_CommandContext& Context)
{
	// Default: trivial success so subclasses that only override CanExecute still "work".
	return true;
}

bool UDP_GameplayCommand::Undo_Implementation(const FDP_CommandContext& Context)
{
	// Default: nothing captured to reverse.
	return false;
}

bool UDP_GameplayCommand::CanExecute_Implementation(const FDP_CommandContext& Context) const
{
	return true;
}

FString UDP_GameplayCommand::GetDisplayName() const
{
	const FString TagStr = CommandTag.IsValid() ? CommandTag.ToString() : TEXT("<untagged>");
	return FString::Printf(TEXT("%s(%s)"), *GetClass()->GetName(), *TagStr);
}

bool UDP_GameplayCommand::ExecuteCommand(const FDP_CommandContext& Context)
{
	// Route through the BlueprintNativeEvent so BP overrides are honored.
	return Execute(Context);
}

bool UDP_GameplayCommand::UndoCommand(const FDP_CommandContext& Context)
{
	if (!IsUndoable())
	{
		UE_LOG(LogDPCmd, Verbose, TEXT("UndoCommand ignored on non-undoable command %s"), *GetDisplayName());
		return false;
	}
	return Undo(Context);
}

bool UDP_GameplayCommand::CanExecuteCommand(const FDP_CommandContext& Context) const
{
	return CanExecute(Context);
}

bool UDP_GameplayCommand::IsUndoable() const
{
	return CommandKind == EDP_CommandKind::Undoable;
}

FGameplayTag UDP_GameplayCommand::GetCommandTag() const
{
	return CommandTag;
}
