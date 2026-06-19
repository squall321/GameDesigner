// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Command/DPCommandHistorySubsystem.h"
#include "Command/DPGameplayCommand.h"
#include "Core/DPLog.h"
#include "Core/DPDeveloperSettings.h"
#include "Engine/World.h"
#include "Stats/Stats.h"
#include "HAL/IConsoleManager.h"

DECLARE_CYCLE_STAT(TEXT("Command Submit"), STAT_DPCmdSubmit, STATGROUP_DesignPatterns);
DECLARE_CYCLE_STAT(TEXT("Command Replay Tick"), STAT_DPCmdReplayTick, STATGROUP_DesignPatterns);
DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("Command Undo Depth"), STAT_DPCmdUndoDepth, STATGROUP_DesignPatterns);

bool UDP_CommandHistorySubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	// Only meaningful in game/PIE worlds — not in editor preview / inactive worlds.
	if (const UWorld* World = Cast<UWorld>(Outer))
	{
		return World->IsGameWorld();
	}
	return true;
}

void UDP_CommandHistorySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (const UDP_DeveloperSettings* Settings = UDP_DeveloperSettings::Get())
	{
		HistoryDepth = FMath::Max(0, Settings->CommandHistoryDepth);
	}

	UE_LOG(LogDPCmd, Log, TEXT("CommandHistory initialized (undo depth = %d) for world '%s'"),
		HistoryDepth, *GetNameSafe(GetWorld()));
}

void UDP_CommandHistorySubsystem::Deinitialize()
{
	StopReplay();
	UndoRing.Reset();
	RedoStack.Reset();
	ReplayStream.Reset();
	SET_DWORD_STAT(STAT_DPCmdUndoDepth, 0);
	Super::Deinitialize();
}

bool UDP_CommandHistorySubsystem::IsTickable() const
{
	// Only tick while actively replaying — no idle cost on the common path.
	return bReplaying;
}

void UDP_CommandHistorySubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bReplaying)
	{
		TickReplay();
	}
}

TStatId UDP_CommandHistorySubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDP_CommandHistorySubsystem, STATGROUP_Tickables);
}

double UDP_CommandHistorySubsystem::StampTime(FDP_CommandContext& Context) const
{
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	Context.TimeStampSeconds = Now;
	return Now;
}

bool UDP_CommandHistorySubsystem::Submit(UDP_GameplayCommand* Command, const FDP_CommandContext& InContext)
{
	SCOPE_CYCLE_COUNTER(STAT_DPCmdSubmit);

	if (!IsValid(Command))
	{
		UE_LOG(LogDPCmd, Warning, TEXT("Submit ignored: null/invalid command."));
		return false;
	}

	FDP_CommandContext Context = InContext;
	StampTime(Context);

	if (!Command->CanExecuteCommand(Context))
	{
		UE_LOG(LogDPCmd, Verbose, TEXT("Submit rejected by CanExecute: %s"), *Command->GetDisplayName());
		return false;
	}

	// Re-outer the command to this subsystem so the ring (and replay stream) own its lifetime
	// independently of the transient submitter (e.g. a queue component that may be destroyed).
	if (Command->GetOuter() != this)
	{
		Command->Rename(nullptr, this, REN_DontCreateRedirectors | REN_DoNotDirty);
	}

	if (!Command->ExecuteCommand(Context))
	{
		UE_LOG(LogDPCmd, Verbose, TEXT("Execute returned false (no effect), not recorded: %s"),
			*Command->GetDisplayName());
		return false;
	}

	const EDP_CommandKind Kind = Command->GetCommandKind();
	FDP_CommandRecord Record;
	Record.Command = Command;
	Record.Context = Context;

	switch (Kind)
	{
	case EDP_CommandKind::ReplayOnly:
		// Replay-only commands NEVER enter the undo ring — they only feed the replay stream.
		ReplayStream.Add(MoveTemp(Record));
		break;

	case EDP_CommandKind::Undoable:
		// A fresh undoable action invalidates the redo branch.
		RedoStack.Reset();
		UndoRing.Add(MoveTemp(Record));
		EnforceDepth();
		break;

	case EDP_CommandKind::Immediate:
	default:
		// Executed and forgotten. A fresh action still invalidates redo.
		RedoStack.Reset();
		break;
	}

	SET_DWORD_STAT(STAT_DPCmdUndoDepth, UndoRing.Num());

	OnCommandExecuted.Broadcast(Command, Command->GetCommandTag());
	OnCommandReplicatedHook(Command, Context);

	UE_LOG(LogDPCmd, Verbose, TEXT("Submit OK: %s (kind=%d, undo=%d, replay=%d)"),
		*Command->GetDisplayName(), (int32)Kind, UndoRing.Num(), ReplayStream.Num());
	return true;
}

bool UDP_CommandHistorySubsystem::Undo()
{
	if (UndoRing.Num() == 0)
	{
		UE_LOG(LogDPCmd, Verbose, TEXT("Undo ignored: ring empty."));
		return false;
	}

	FDP_CommandRecord Record = UndoRing.Pop();
	if (!Record.IsValid())
	{
		SET_DWORD_STAT(STAT_DPCmdUndoDepth, UndoRing.Num());
		return false;
	}

	const bool bOk = Record.Command->UndoCommand(Record.Context);
	const FGameplayTag Tag = Record.Command->GetCommandTag();
	UDP_GameplayCommand* Cmd = Record.Command;

	if (bOk)
	{
		RedoStack.Add(MoveTemp(Record));
		OnCommandUndone.Broadcast(Cmd, Tag);
		UE_LOG(LogDPCmd, Verbose, TEXT("Undo OK: %s"), *Cmd->GetDisplayName());
	}
	else
	{
		// Undo failed — do not push to redo, but the entry is already off the ring.
		UE_LOG(LogDPCmd, Warning, TEXT("Undo failed (dropped): %s"), *Cmd->GetDisplayName());
	}

	SET_DWORD_STAT(STAT_DPCmdUndoDepth, UndoRing.Num());
	return bOk;
}

bool UDP_CommandHistorySubsystem::Redo()
{
	if (RedoStack.Num() == 0)
	{
		UE_LOG(LogDPCmd, Verbose, TEXT("Redo ignored: stack empty."));
		return false;
	}

	FDP_CommandRecord Record = RedoStack.Pop();
	if (!Record.IsValid())
	{
		return false;
	}

	const bool bOk = Record.Command->ExecuteCommand(Record.Context);
	const FGameplayTag Tag = Record.Command->GetCommandTag();
	UDP_GameplayCommand* Cmd = Record.Command;

	if (bOk)
	{
		UndoRing.Add(MoveTemp(Record));
		EnforceDepth();
		OnCommandRedone.Broadcast(Cmd, Tag);
		UE_LOG(LogDPCmd, Verbose, TEXT("Redo OK: %s"), *Cmd->GetDisplayName());
	}
	else
	{
		UE_LOG(LogDPCmd, Warning, TEXT("Redo failed (dropped): %s"), *Cmd->GetDisplayName());
	}

	SET_DWORD_STAT(STAT_DPCmdUndoDepth, UndoRing.Num());
	return bOk;
}

void UDP_CommandHistorySubsystem::Clear()
{
	UndoRing.Reset();
	RedoStack.Reset();
	SET_DWORD_STAT(STAT_DPCmdUndoDepth, 0);
	UE_LOG(LogDPCmd, Log, TEXT("CommandHistory undo/redo cleared."));
}

void UDP_CommandHistorySubsystem::ClearReplay()
{
	StopReplay();
	ReplayStream.Reset();
	UE_LOG(LogDPCmd, Log, TEXT("CommandHistory replay stream cleared."));
}

void UDP_CommandHistorySubsystem::EnforceDepth()
{
	if (HistoryDepth <= 0)
	{
		// Depth 0 disables undo recording entirely.
		UndoRing.Reset();
		return;
	}
	while (UndoRing.Num() > HistoryDepth)
	{
		UndoRing.RemoveAt(0);
	}
}

void UDP_CommandHistorySubsystem::StartReplay()
{
	if (ReplayStream.Num() == 0)
	{
		UE_LOG(LogDPCmd, Warning, TEXT("StartReplay ignored: replay stream empty."));
		return;
	}

	const UWorld* World = GetWorld();
	ReplayStartWorldSeconds = World ? World->GetTimeSeconds() : 0.0;
	ReplayBaseTimeSeconds = ReplayStream[0].Context.TimeStampSeconds;
	ReplayCursor = 0;
	bReplaying = true;

	UE_LOG(LogDPCmd, Log, TEXT("Replay started: %d commands."), ReplayStream.Num());
}

void UDP_CommandHistorySubsystem::StopReplay()
{
	if (bReplaying)
	{
		UE_LOG(LogDPCmd, Log, TEXT("Replay stopped at cursor %d/%d."), ReplayCursor, ReplayStream.Num());
	}
	bReplaying = false;
	ReplayCursor = 0;
}

void UDP_CommandHistorySubsystem::TickReplay()
{
	SCOPE_CYCLE_COUNTER(STAT_DPCmdReplayTick);

	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	const double Elapsed = Now - ReplayStartWorldSeconds;

	// Re-execute every command whose relative timestamp has arrived this frame.
	while (ReplayCursor < ReplayStream.Num())
	{
		const FDP_CommandRecord& Record = ReplayStream[ReplayCursor];
		const double RelativeTime = Record.Context.TimeStampSeconds - ReplayBaseTimeSeconds;
		if (RelativeTime > Elapsed)
		{
			break; // not yet time for this one
		}

		if (Record.IsValid())
		{
			Record.Command->ExecuteCommand(Record.Context);
			UE_LOG(LogDPCmd, Verbose, TEXT("Replay exec [%d]: %s @ %.3fs"),
				ReplayCursor, *Record.Command->GetDisplayName(), RelativeTime);
		}
		++ReplayCursor;
	}

	if (ReplayCursor >= ReplayStream.Num())
	{
		UE_LOG(LogDPCmd, Log, TEXT("Replay complete (%d commands)."), ReplayStream.Num());
		StopReplay();
	}
}

void UDP_CommandHistorySubsystem::DumpHistory() const
{
	UE_LOG(LogDPCmd, Log, TEXT("==== Command History Dump (world '%s') ===="), *GetNameSafe(GetWorld()));
	UE_LOG(LogDPCmd, Log, TEXT("Undo ring (%d, depth cap %d):"), UndoRing.Num(), HistoryDepth);
	for (int32 i = 0; i < UndoRing.Num(); ++i)
	{
		const FDP_CommandRecord& R = UndoRing[i];
		UE_LOG(LogDPCmd, Log, TEXT("  [%d] %s target=%s @ %.3fs"),
			i,
			R.Command ? *R.Command->GetDisplayName() : TEXT("<null>"),
			*R.Context.Target.ToDebugString(),
			R.Context.TimeStampSeconds);
	}
	UE_LOG(LogDPCmd, Log, TEXT("Redo stack (%d):"), RedoStack.Num());
	for (int32 i = 0; i < RedoStack.Num(); ++i)
	{
		const FDP_CommandRecord& R = RedoStack[i];
		UE_LOG(LogDPCmd, Log, TEXT("  [%d] %s"), i, R.Command ? *R.Command->GetDisplayName() : TEXT("<null>"));
	}
	UE_LOG(LogDPCmd, Log, TEXT("Replay stream (%d):"), ReplayStream.Num());
	for (int32 i = 0; i < ReplayStream.Num(); ++i)
	{
		const FDP_CommandRecord& R = ReplayStream[i];
		UE_LOG(LogDPCmd, Log, TEXT("  [%d] %s @ %.3fs"),
			i, R.Command ? *R.Command->GetDisplayName() : TEXT("<null>"), R.Context.TimeStampSeconds);
	}
	UE_LOG(LogDPCmd, Log, TEXT("=========================================="));
}

FString UDP_CommandHistorySubsystem::GetDPDebugString_Implementation() const
{
	return FString::Printf(TEXT("CommandHistory: undo=%d redo=%d replay=%d%s"),
		UndoRing.Num(), RedoStack.Num(), ReplayStream.Num(),
		bReplaying ? TEXT(" [REPLAYING]") : TEXT(""));
}

// ---------------------------------------------------------------------------
// Console command backing: DP.Cmd.Dump / DP.Cmd.Undo / DP.Cmd.Redo.
// Each resolves the history subsystem from the world the command was issued in.
// ---------------------------------------------------------------------------
namespace DPCmdConsole
{
	static UDP_CommandHistorySubsystem* ResolveFromWorld(UWorld* World)
	{
		return (World && World->IsGameWorld())
			? World->GetSubsystem<UDP_CommandHistorySubsystem>()
			: nullptr;
	}
}

static FAutoConsoleCommandWithWorld GDPCmdDump(
	TEXT("DP.Cmd.Dump"),
	TEXT("Dump the DesignPatterns command undo/redo/replay history to the log."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		if (UDP_CommandHistorySubsystem* History = DPCmdConsole::ResolveFromWorld(World))
		{
			History->DumpHistory();
		}
		else
		{
			UE_LOG(LogDPCmd, Warning, TEXT("DP.Cmd.Dump: no CommandHistory in current world."));
		}
	}));

static FAutoConsoleCommandWithWorld GDPCmdUndo(
	TEXT("DP.Cmd.Undo"),
	TEXT("Undo the most recent undoable DesignPatterns command."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		if (UDP_CommandHistorySubsystem* History = DPCmdConsole::ResolveFromWorld(World))
		{
			History->Undo();
		}
	}));

static FAutoConsoleCommandWithWorld GDPCmdRedo(
	TEXT("DP.Cmd.Redo"),
	TEXT("Redo the most recently undone DesignPatterns command."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		if (UDP_CommandHistorySubsystem* History = DPCmdConsole::ResolveFromWorld(World))
		{
			History->Redo();
		}
	}));
