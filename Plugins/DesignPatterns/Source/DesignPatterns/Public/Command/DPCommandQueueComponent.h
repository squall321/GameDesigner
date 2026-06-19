// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Command/DPCommandContext.h"

// FInstancedStruct lives in the StructUtils plugin on UE 5.3/5.4 and is merged into
// CoreUObject in 5.5+. Include the right header for the engine band.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

// NOTE: the .generated.h MUST be the last include (UnrealHeaderTool requirement).
#include "DPCommandQueueComponent.generated.h"

class UDP_GameplayCommand;
class UDP_CommandHistorySubsystem;

/**
 * Per-actor intent bridge for the Command pattern.
 *
 * Gameplay code (input handlers, AI, abilities) talks to THIS component rather than to the
 * history subsystem directly. The component is the single per-actor chokepoint: it builds a
 * stable FDP_CommandContext from its owner, then forwards the command into the world's
 * UDP_CommandHistorySubsystem. This keeps the "where do commands go" decision in one place and
 * gives subclasses one virtual to override for buffering, validation gates, or (future) network
 * forwarding.
 *
 * This base is intentionally local-only and does NOT replicate. A networked variant is an
 * explicit extension point (ForwardToHistory is virtual); implementing a FFastArraySerializer-
 * backed command stream is out of scope for this component.
 */
UCLASS(ClassGroup = (DesignPatterns), meta = (BlueprintSpawnableComponent),
	HideCategories = (ComponentReplication, Activation, Cooking, AssetUserData))
class DESIGNPATTERNS_API UDP_CommandQueueComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDP_CommandQueueComponent();

	/**
	 * Build a context from this component's owner (as Target + Instigator) and submit the command
	 * through the history subsystem. Returns true if the command executed. Convenience entry point
	 * for the common "the owning actor is the subject" case.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Command")
	bool SubmitCommand(UDP_GameplayCommand* Command);

	/**
	 * Submit a command with an explicit, caller-built context (e.g. a different target than the
	 * owner). Returns true if the command executed.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Command")
	bool SubmitCommandWithContext(UDP_GameplayCommand* Command, const FDP_CommandContext& Context);

	/**
	 * Construct a command of CommandClass owned by this component, fill its context from the owner
	 * plus the supplied Params, and submit it. Returns the created command (or nullptr on failure)
	 * so callers can inspect it. The command's Outer is this component, so it is GC-safe until the
	 * history subsystem re-outers it on a successful, recorded submit.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Command",
		meta = (DeterminesOutputType = "CommandClass"))
	UDP_GameplayCommand* MakeAndSubmit(TSubclassOf<UDP_GameplayCommand> CommandClass, FInstancedStruct Params);

	/** Build a stable context with this component's owner as both Target and Instigator. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Command")
	FDP_CommandContext MakeContextForOwner(FInstancedStruct Params) const;

	/** Resolve the world's command history subsystem (null-safe). Cached after first lookup. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Command")
	UDP_CommandHistorySubsystem* GetHistory() const;

protected:
	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	//~ End UActorComponent

	/**
	 * The actual forwarding step — the documented extension point. The base implementation submits
	 * straight to the local history subsystem. A networked subclass would override this to forward
	 * the intent to the server / remote clients before (or instead of) local submission.
	 * Returns true if the command was accepted/executed locally.
	 */
	virtual bool ForwardToHistory(UDP_GameplayCommand* Command, const FDP_CommandContext& Context);

private:
	/** Cached, non-owning handle to the world command history subsystem. Re-resolved if stale. */
	UPROPERTY(Transient)
	mutable TWeakObjectPtr<UDP_CommandHistorySubsystem> CachedHistory;
};
