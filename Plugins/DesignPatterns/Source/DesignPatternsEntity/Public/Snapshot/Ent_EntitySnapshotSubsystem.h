// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "Identity/Seam_EntityId.h"
#include "Snapshot/Ent_EntitySnapshot.h"
#include "Ent_EntitySnapshotSubsystem.generated.h"

class UEnt_EntityComponent;
class UEnt_EntityRegistrySubsystem;

/**
 * Capture / restore / rewind service for entities.
 *
 * CAPTURE reuses ISeam_Persistable::CaptureState on the entity component (yielding the core
 * FEnt_EntitySaveData) and gathers the extras (links, replicated tags, transform) into the snapshot.
 *
 * RESTORE is AUTHORITY-ONLY (own GetNetMode check): it calls ISeam_Persistable::RestoreState then
 * rebuilds the relationship links and replicated tags from the snapshot. Clients receive the restored
 * state through replication — they never restore locally.
 *
 * REWIND keeps a per-entity ring buffer of recent snapshots (capped at MaxRewindFrames) so a debug/replay
 * tool can roll an entity back. Snapshots are LOCAL/SAVE only and are NEVER replicated.
 */
UCLASS()
class DESIGNPATTERNSENTITY_API UEnt_EntitySnapshotSubsystem : public UDP_WorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/** True on server / standalone, false on a network client (own helper, no base). */
	bool HasWorldAuthority() const
	{
		const UWorld* W = GetWorld();
		return W && W->GetNetMode() != NM_Client;
	}

	/** Capture a single entity (by id) into a snapshot. Safe on server + clients (read-only). */
	UFUNCTION(BlueprintCallable, Category = "Entity|Snapshot")
	FEnt_EntitySnapshot CaptureEntity(FSeam_EntityId EntityId) const;

	/** Restore a single entity from a snapshot. AUTHORITY ONLY. Returns true if the entity was found. */
	UFUNCTION(BlueprintCallable, Category = "Entity|Snapshot")
	bool RestoreEntity(FSeam_EntityId EntityId, const FEnt_EntitySnapshot& Snapshot);

	/** Capture every registered entity into a world snapshot. Read-only. */
	UFUNCTION(BlueprintCallable, Category = "Entity|Snapshot")
	FEnt_WorldSnapshot CaptureWorld() const;

	/** Restore a whole world snapshot. AUTHORITY ONLY. Returns the number of entities restored. */
	UFUNCTION(BlueprintCallable, Category = "Entity|Snapshot")
	int32 RestoreWorld(const FEnt_WorldSnapshot& Snapshot);

	/** Push a capture of EntityId onto its rewind ring buffer. AUTHORITY ONLY. */
	UFUNCTION(BlueprintCallable, Category = "Entity|Snapshot")
	void PushRewindSnapshot(FSeam_EntityId EntityId);

	/**
	 * Rewind EntityId by FramesBack entries in its ring buffer (0 = the most recent). AUTHORITY ONLY.
	 * Returns true if a snapshot at that depth existed and was restored.
	 */
	UFUNCTION(BlueprintCallable, Category = "Entity|Snapshot")
	bool RewindTo(FSeam_EntityId EntityId, int32 FramesBack);

	//~ Begin UDP_WorldSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_WorldSubsystem

	/** Maximum snapshots kept per entity in the rewind ring buffer. Tunable. */
	UPROPERTY(EditAnywhere, Category = "Entity|Snapshot", meta = (ClampMin = "1"))
	int32 MaxRewindFrames = 32;

private:
	/** Per-entity rewind ring (oldest first). Local/save only — never replicated. */
	TMap<FSeam_EntityId, TArray<FEnt_EntitySnapshot>> RewindBuffers;

	/** Resolve the registry (null-safe). */
	UEnt_EntityRegistrySubsystem* GetRegistry() const;

	/** Build a snapshot from a live entity component. */
	FEnt_EntitySnapshot CaptureFromComponent(UEnt_EntityComponent* Entity) const;

	/** Apply a snapshot to a live entity component (authority). */
	bool ApplyToComponent(UEnt_EntityComponent* Entity, const FEnt_EntitySnapshot& Snapshot);

	/** The current capture timestamp (deterministic where a sim clock exists, else world time). */
	double GetCaptureTime() const;
};
