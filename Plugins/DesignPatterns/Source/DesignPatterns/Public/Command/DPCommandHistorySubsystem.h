// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "Command/DPCommandContext.h"
#include "DPCommandHistorySubsystem.generated.h"

class UDP_GameplayCommand;

/**
 * One recorded entry: the command object plus the exact context it was executed with.
 * The context is stored alongside the command so undo/redo and replay use the SAME stable
 * targets/params the command originally ran with, independent of any live caller state.
 */
USTRUCT()
struct DESIGNPATTERNS_API FDP_CommandRecord
{
	GENERATED_BODY()

	/** The command instance. UPROPERTY TObjectPtr keeps it GC-alive while it lives in a buffer. */
	UPROPERTY()
	TObjectPtr<UDP_GameplayCommand> Command = nullptr;

	/** The context the command executed with (stable targets + params + timestamp). */
	UPROPERTY()
	FDP_CommandContext Context;

	bool IsValid() const { return Command != nullptr; }
};

/** Fired after a command successfully executes via Submit. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDP_OnCommandExecuted, UDP_GameplayCommand*, Command, FGameplayTag, CommandTag);
/** Fired after a command is successfully undone. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDP_OnCommandUndone, UDP_GameplayCommand*, Command, FGameplayTag, CommandTag);
/** Fired after a previously-undone command is successfully redone. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDP_OnCommandRedone, UDP_GameplayCommand*, Command, FGameplayTag, CommandTag);

/**
 * World-scoped undo/redo/replay history for gameplay commands — the single chokepoint through
 * which all command intent flows (Submit).
 *
 * Design:
 *  - Bounded undo RING whose depth comes from UDP_DeveloperSettings::CommandHistoryDepth. Pushing
 *    past the cap discards the oldest entry (oldest history is the cheapest to forget).
 *  - A redo stack that is cleared whenever a fresh non-replay command is submitted (standard
 *    "new action invalidates the redo branch" semantics).
 *  - A SEPARATE, append-only replay stream for EDP_CommandKind::ReplayOnly commands. Replay-only
 *    commands NEVER enter the undo ring — recording input for playback is orthogonal to undo.
 *  - Tickable so callers can drive time-accurate replay playback from the recorded timestamps.
 *
 * It derives from UTickableWorldSubsystem (not the project's UDP_WorldSubsystem) because it needs
 * a real tick for replay playback; it still mirrors the DP debug-string convention.
 */
UCLASS()
class DESIGNPATTERNS_API UDP_CommandHistorySubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	//~ End USubsystem

	//~ Begin FTickableGameObject (via UTickableWorldSubsystem)
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
	//~ End FTickableGameObject

	// ---- Core command API (the single chokepoint) ----

	/**
	 * Validate (CanExecute), execute, and record a command. Immediate commands run and are
	 * forgotten; Undoable commands enter the ring (clearing the redo stack); ReplayOnly commands
	 * are appended to the replay stream only. Returns true if the command executed.
	 * The submitted command is re-outered to this subsystem so the ring safely owns its lifetime.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Command")
	bool Submit(UDP_GameplayCommand* Command, const FDP_CommandContext& Context);

	/** Undo the most recent undoable command. Returns false if the undo ring is empty. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Command")
	bool Undo();

	/** Re-apply the most recently undone command. Returns false if the redo stack is empty. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Command")
	bool Redo();

	/** Drop the entire undo ring and redo stack (does NOT touch the replay stream). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Command")
	void Clear();

	/** Drop the recorded replay stream (does NOT touch undo/redo). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Command")
	void ClearReplay();

	// ---- Replay playback ----

	/**
	 * Begin time-accurate playback of the recorded replay stream from the start. Commands are
	 * re-executed on tick as wall-clock time passes their recorded relative timestamp.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Command")
	void StartReplay();

	/** Stop any in-progress replay playback. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Command")
	void StopReplay();

	/** True while replay playback is actively driving commands from the stream. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Command")
	bool IsReplaying() const { return bReplaying; }

	// ---- Introspection (backing DP.Cmd.*) ----

	/** Number of undoable commands currently available to undo. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Command")
	int32 GetUndoableCount() const { return UndoRing.Num(); }

	/** Number of commands currently available to redo. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Command")
	int32 GetRedoableCount() const { return RedoStack.Num(); }

	/** Number of commands captured in the replay stream. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Command")
	int32 GetReplayCount() const { return ReplayStream.Num(); }

	/** Dump the undo ring, redo stack and replay stream to the log — backing for DP.Cmd.Dump. */
	void DumpHistory() const;

	/** One-line status for on-screen / gameplay-debugger output. */
	UFUNCTION(BlueprintNativeEvent, Category = "DesignPatterns|Debug")
	FString GetDPDebugString() const;
	virtual FString GetDPDebugString_Implementation() const;

	// ---- Delegates ----

	/** Broadcast after a command successfully executes via Submit. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Command")
	FDP_OnCommandExecuted OnCommandExecuted;

	/** Broadcast after a command is successfully undone. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Command")
	FDP_OnCommandUndone OnCommandUndone;

	/** Broadcast after a previously-undone command is successfully redone. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Command")
	FDP_OnCommandRedone OnCommandRedone;

protected:
	/**
	 * Extension point for a future networked command component: override to mirror locally-applied
	 * commands to remote machines. Called for every command that successfully executes via Submit.
	 * The base implementation does nothing (local-only). Deliberately NOT a FastArraySerializer here.
	 */
	virtual void OnCommandReplicatedHook(UDP_GameplayCommand* Command, const FDP_CommandContext& Context) {}

private:
	/**
	 * Bounded undo ring (most recent at the back). Capacity = CommandHistoryDepth; pushing past it
	 * drops the front. TArray of UPROPERTY-bearing structs keeps the command objects GC-alive.
	 */
	UPROPERTY()
	TArray<FDP_CommandRecord> UndoRing;

	/** Commands that were undone and may be redone. Cleared on any fresh non-replay submit. */
	UPROPERTY()
	TArray<FDP_CommandRecord> RedoStack;

	/** Append-only stream of ReplayOnly commands, ordered by submission time. */
	UPROPERTY()
	TArray<FDP_CommandRecord> ReplayStream;

	/** Resolved ring depth from settings, cached at Initialize. 0 disables undo recording. */
	int32 HistoryDepth = 128;

	/** Replay playback state. */
	bool bReplaying = false;
	double ReplayStartWorldSeconds = 0.0;
	double ReplayBaseTimeSeconds = 0.0;
	int32 ReplayCursor = 0;

	/** Trim the undo ring down to HistoryDepth, discarding oldest entries. */
	void EnforceDepth();

	/** Stamp the context's timestamp from the world clock (used for replay ordering). */
	double StampTime(FDP_CommandContext& Context) const;

	/** Advance replay playback by re-executing any commands whose time has arrived. */
	void TickReplay();
};
