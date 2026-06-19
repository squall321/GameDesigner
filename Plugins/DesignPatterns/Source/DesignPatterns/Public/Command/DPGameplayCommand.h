// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "Command/DPCommandContext.h"
#include "Command/DPGameplayCommandInterface.h"
#include "DPGameplayCommand.generated.h"

/**
 * How a command participates in the history subsystem.
 */
UENUM(BlueprintType)
enum class EDP_CommandKind : uint8
{
	/** Fire-and-forget. Executes, never enters the undo ring. */
	Immediate,

	/** Executes AND is pushed onto the undo ring so it can be reversed / re-applied. */
	Undoable,

	/**
	 * Recorded for deterministic replay only. Routed to the SEPARATE replay stream and never
	 * touches the undo ring (replaying is not "undoing"). Typically used for input capture.
	 */
	ReplayOnly
};

/**
 * Abstract, Blueprintable base for UObject-form commands.
 *
 * Implements IDP_GameplayCommand by forwarding each operation to a BlueprintNativeEvent, so a
 * designer can author the whole command in a Blueprint subclass while C++ subclasses override the
 * native _Implementation. Instances are transient UObjects owned by whoever creates them (the
 * queue component or the history subsystem) and are kept GC-alive by the ring buffer while recorded.
 *
 * GC/ownership: create with NewObject<MyCommand>(Outer) where Outer is the submitting component or
 * the history subsystem so the command shares a sensible lifetime; the history ring then holds a
 * UPROPERTY TObjectPtr to keep it alive for undo/redo.
 */
UCLASS(Abstract, Blueprintable, BlueprintType, EditInlineNew, meta = (ShowWorldContextPin))
class DESIGNPATTERNS_API UDP_GameplayCommand : public UObject, public IDP_GameplayCommand
{
	GENERATED_BODY()

public:
	UDP_GameplayCommand();

	/**
	 * Designer-overridable execute. Default native impl returns true (no-op success) so trivial
	 * commands need only set CommandTag. Override in C++ (_Implementation) or Blueprint.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Command")
	bool Execute(const FDP_CommandContext& Context);
	virtual bool Execute_Implementation(const FDP_CommandContext& Context);

	/** Designer-overridable undo. Default native impl returns false (nothing to reverse). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Command")
	bool Undo(const FDP_CommandContext& Context);
	virtual bool Undo_Implementation(const FDP_CommandContext& Context);

	/** Designer-overridable pre-check. Default native impl returns true. Must be side-effect free. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Command")
	bool CanExecute(const FDP_CommandContext& Context) const;
	virtual bool CanExecute_Implementation(const FDP_CommandContext& Context) const;

	/** The kind drives routing (immediate vs undo ring vs replay stream). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Command")
	EDP_CommandKind GetCommandKind() const { return CommandKind; }

	/** Set the command kind (usually at construction / in the BP defaults). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Command")
	void SetCommandKind(EDP_CommandKind InKind) { CommandKind = InKind; }

	/** Identity/category tag for this command instance. Settable so generic commands can be reused. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Command")
	FGameplayTag GetTag() const { return CommandTag; }

	/** Assign the identity tag (e.g. DP.Cmd.Move). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Command")
	void SetTag(FGameplayTag InTag) { CommandTag = InTag; }

	/** Human-readable label for logs and DP.Cmd.Dump. Defaults to the class + tag. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Command")
	virtual FString GetDisplayName() const;

	//~ Begin IDP_GameplayCommand
	virtual bool ExecuteCommand(const FDP_CommandContext& Context) override;
	virtual bool UndoCommand(const FDP_CommandContext& Context) override;
	virtual bool CanExecuteCommand(const FDP_CommandContext& Context) const override;
	virtual bool IsUndoable() const override;
	virtual FGameplayTag GetCommandTag() const override;
	//~ End IDP_GameplayCommand

protected:
	/** Routing kind. Subclasses/Blueprints set this in their defaults. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Command")
	EDP_CommandKind CommandKind = EDP_CommandKind::Immediate;

	/** Identity tag, ideally a child of DPNativeTags::Cmd (e.g. DP.Cmd.Move). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Command", meta = (Categories = "DP.Cmd"))
	FGameplayTag CommandTag;
};
