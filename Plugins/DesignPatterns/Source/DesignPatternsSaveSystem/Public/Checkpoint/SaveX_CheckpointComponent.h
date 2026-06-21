// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "UObject/ScriptInterface.h"
#include "SaveX_CheckpointComponent.generated.h"

class ISeam_SaveSlotManager;
class UDP_SaveGameSubsystem;
class UDP_SaveGame;
class USaveX_AutosaveSubsystem;

/**
 * Fired (authority side) after a checkpoint record completes. bSuccess reflects whether the underlying save
 * write succeeded. Replicated state is NOT carried here; this is a local authority notification for HUD/SFX.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSaveX_OnCheckpointRecorded, FGameplayTag, CheckpointId, bool, bSuccess);

/**
 * Fired (authority side) after RestoreFromCheckpoint completes its load attempt. bSuccess is false when no
 * checkpoint slot existed or the load failed; the caller can then fall back to a normal respawn.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSaveX_OnCheckpointRestored, bool, bSuccess);

/**
 * Records and restores a single "last checkpoint" for its owning actor.
 *
 * Design:
 *  - This component is the AUTHORITY-only driver of the checkpoint feature. It does not own the save byte
 *    pipeline; it ENGINE-WRAPS the core UDP_SaveGameSubsystem (via the slot manager seam) and writes to a
 *    single reserved checkpoint slot from settings. No new serialization is invented here.
 *  - The only piece of replicated state is the last-checkpoint marker (id + transform) carried on this
 *    UActorComponent so clients can show "checkpoint reached" feedback and so a late-joining proxy knows the
 *    most recent checkpoint id. Every mutator guards HasAuthority() at the top and early-returns on clients.
 *  - Restore is authority-guarded: it asks the save subsystem to load the checkpoint slot and, on success,
 *    re-applies the recorded transform to the owner (a respawn-to-checkpoint). Persistable subobjects restore
 *    their own state through their own authority-guarded RestoreState contract; this component does not reach
 *    into them.
 *
 * The component intentionally degrades to an inert (logged) no-op when the slot manager seam / save subsystem
 * is unavailable, so it is safe to place on actors in worlds without a configured save backend.
 */
UCLASS(ClassGroup = (DesignPatterns), meta = (BlueprintSpawnableComponent),
	HideCategories = (Object, ComponentReplication, Activation, Cooking, Collision))
class DESIGNPATTERNSSAVESYSTEM_API USaveX_CheckpointComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USaveX_CheckpointComponent();

	//~ Begin UActorComponent
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent

	/**
	 * AUTHORITY ONLY. Record a checkpoint at the owner's current transform under the given logical id and
	 * write the reserved checkpoint slot via the wrapped save subsystem. On a client this is a no-op.
	 *
	 * @param CheckpointId   Designer-facing logical id for this checkpoint (e.g. a region/area tag). May be
	 *                       empty; the marker still records the transform.
	 * @return True if the write was kicked off on authority; false if not authoritative or no save backend.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Save|Checkpoint")
	bool RecordCheckpoint(FGameplayTag CheckpointId);

	/**
	 * AUTHORITY ONLY. Restore the owner from the reserved checkpoint slot: loads the slot through the save
	 * subsystem and, on success, snaps the owner back to the recorded checkpoint transform. On a client this
	 * is a no-op. Designed to back a respawn-on-death / restore-on-load flow.
	 *
	 * @return True if a restore was started on authority; false if not authoritative, no backend, or no
	 *         checkpoint exists yet (caller should then fall back to a normal respawn).
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Save|Checkpoint")
	bool RestoreFromCheckpoint();

	/** True once at least one checkpoint has been recorded (replicated marker is valid). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Save|Checkpoint")
	bool HasCheckpoint() const { return bHasCheckpoint; }

	/** The most recently recorded checkpoint id (replicated; empty if none). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Save|Checkpoint")
	FGameplayTag GetCheckpointId() const { return LastCheckpointId; }

	/** The most recently recorded checkpoint transform (replicated; identity if none). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Save|Checkpoint")
	FTransform GetCheckpointTransform() const { return LastCheckpointTransform; }

	/** Authority + client notification that a checkpoint was recorded (fired locally where recorded). */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Save|Checkpoint")
	FSaveX_OnCheckpointRecorded OnCheckpointRecorded;

	/** Authority notification that a restore attempt finished. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Save|Checkpoint")
	FSaveX_OnCheckpointRestored OnCheckpointRestored;

protected:
	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	//~ End UActorComponent

	/** RepNotify: a new checkpoint marker arrived on a proxy — broadcast the local "recorded" event. */
	UFUNCTION()
	void OnRep_CheckpointId();

private:
	/** Whether a checkpoint has ever been recorded. Replicated so proxies can show feedback. */
	UPROPERTY(ReplicatedUsing = OnRep_CheckpointId)
	bool bHasCheckpoint = false;

	/** Logical id of the last checkpoint. Replicated. */
	UPROPERTY(ReplicatedUsing = OnRep_CheckpointId)
	FGameplayTag LastCheckpointId;

	/** Owner transform captured at the last checkpoint (used by restore). Replicated. */
	UPROPERTY(Replicated)
	FTransform LastCheckpointTransform = FTransform::Identity;

	/**
	 * Resolve the slot manager seam (ISeam_SaveSlotManager) from the service locator. Returns an unset
	 * TScriptInterface when no backend is registered; callers treat that as the inert default.
	 */
	TScriptInterface<ISeam_SaveSlotManager> ResolveSlotManager() const;

	/** Resolve the core save subsystem we wrap (GI-scoped). May be null in CDO/editor contexts. */
	UDP_SaveGameSubsystem* ResolveSaveSubsystem() const;

	/** Resolve the sibling autosave subsystem (to kick the ring on checkpoint when enabled). May be null. */
	USaveX_AutosaveSubsystem* ResolveAutosaveSubsystem() const;

	/** The reserved checkpoint slot name from settings (with a defensive fallback when the CDO is null). */
	FString GetCheckpointSlotName() const;

	/** Build a fresh save object capturing the minimal checkpoint payload (owner transform + id). */
	UDP_SaveGame* BuildCheckpointSaveObject() const;
};
