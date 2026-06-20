// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "Grid/Seam_GridCoord.h"
#include "Grid/Seam_TileProviderRead.h"
#include "Identity/Seam_EntityId.h"
#include "World/SimGrid_CoordTypes.h"

// FInstancedStruct version-gated include (ApplyCell payload arguments).
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "SimGrid_WorldSubsystem.generated.h"

class ASimGrid_ChunkReplicator;

/**
 * World-scoped grid service: the single source of coordinate math and read snapshots for the grid,
 * and the authority-side router that owns per-chunk replication carriers.
 *
 * RESPONSIBILITIES
 *  - Implements ISeam_TileProviderRead (pure coord math + tri-state cell read). Registers itself under
 *    the configured DP.Service.SimGrid.TileProvider tag (WeakObserved) so consumers (agents, economy)
 *    read the grid through the seam without depending on SimGrid.
 *  - Holds NO replicated state itself (subsystems are never replicated). On authority it lazily spawns
 *    one ASimGrid_ChunkReplicator per occupied chunk and routes every mutation to that carrier, whose
 *    authority-guarded mutators are the real source of truth.
 *
 * Reads are client-safe and tri-state: a cell whose owning chunk has no carrier on this machine returns
 * Unknown (not Empty), so clients never mistake missing replication for authoritative emptiness.
 */
UCLASS()
class DESIGNPATTERNSSIMGRID_API USimGrid_WorldSubsystem : public UDP_WorldSubsystem, public ISeam_TileProviderRead
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/**
	 * UWorldSubsystem has no HasWorldAuthority; declare our own. True on the server / standalone
	 * (anything that is not a pure net client), gating all carrier spawning and mutation.
	 */
	bool HasWorldAuthority() const
	{
		const UWorld* W = GetWorld();
		return W && W->GetNetMode() != NM_Client;
	}

	//~ Begin ISeam_TileProviderRead (pure coord math + tri-state read; client-safe)
	virtual FSeam_CellCoord WorldToCell_Implementation(const FVector& WorldLocation) const override;
	virtual FVector CellToWorld_Implementation(const FSeam_CellCoord& Cell, bool bCenter) const override;
	virtual bool IsValidCell_Implementation(const FSeam_CellCoord& Cell) const override;
	virtual FSeam_CellSnapshot GetCellSnapshot_Implementation(const FSeam_CellCoord& Cell) const override;
	virtual float GetCellSize_Implementation() const override;
	//~ End ISeam_TileProviderRead

	// --- Authority mutators: delegate to the per-chunk carrier (each guards authority too) ---

	/**
	 * Place/overwrite a cell. AUTHORITY ONLY. Lazily spawns the owning chunk carrier if needed and
	 * forwards to its ApplyCell. Returns true if a change was applied.
	 */
	UFUNCTION(BlueprintCallable, Category = "SimGrid|World")
	bool SetCell(const FSeam_CellCoord& Cell, FGameplayTag TileTypeTag);

	/**
	 * Place/overwrite a cell with an explicit payload. AUTHORITY ONLY. When Payload is empty, the
	 * carrier seeds it from the tile type's DefaultPayloadTemplate (resolved from the data registry).
	 */
	bool SetCellWithPayload(const FSeam_CellCoord& Cell, const FGameplayTag& TileTypeTag, const FInstancedStruct& Payload);

	/** Clear a cell back to empty. AUTHORITY ONLY. Returns true if a tile was present. */
	UFUNCTION(BlueprintCallable, Category = "SimGrid|World")
	bool ClearCell(const FSeam_CellCoord& Cell);

	/** Claim cells for an owner. AUTHORITY ONLY. Cells are grouped by chunk and routed per carrier. */
	int32 ClaimCells(const TArray<FSeam_CellCoord>& Cells, const FSeam_EntityId& OwnerId);

	/** Release ownership of cells. AUTHORITY ONLY. */
	int32 ReleaseCells(const TArray<FSeam_CellCoord>& Cells);

	/** The owner of a cell, or an invalid id if unowned / unknown on this machine. Client-safe read. */
	UFUNCTION(BlueprintCallable, Category = "SimGrid|World")
	FSeam_EntityId GetCellOwner(const FSeam_CellCoord& Cell) const;

	/** Configured cell size in world units (from SimGrid settings). */
	UFUNCTION(BlueprintPure, Category = "SimGrid|World")
	float GetCellSize() const { return CachedCellSize; }

	/** Configured chunk size in cells (from SimGrid settings). */
	UFUNCTION(BlueprintPure, Category = "SimGrid|World")
	FIntPoint GetChunkSize() const { return CachedChunkSize; }

	/**
	 * Find the carrier owning a chunk, optionally spawning it on authority. On clients (or when
	 * bSpawnIfMissing is false) returns the existing carrier or null. Prunes stale map entries.
	 */
	ASimGrid_ChunkReplicator* GetOrSpawnChunkCarrier(const FSimGrid_ChunkCoord& Chunk, bool bSpawnIfMissing);

	/** Read-only access to all currently-known chunk carriers (for save capture / debug). */
	void GetAllChunkCarriers(TArray<ASimGrid_ChunkReplicator*>& OutCarriers) const;

	//~ Begin UDP_WorldSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_WorldSubsystem

private:
	/** Cached cell size from settings, snapshotted at Initialize so reads are branch-light. */
	float CachedCellSize = 100.f;

	/** Cached chunk size from settings (each axis >= 1). */
	FIntPoint CachedChunkSize = FIntPoint(16, 16);

	/** Whether the grid uses finite bounds (from settings). */
	bool bUseBounds = false;
	FIntPoint BoundsMinCell = FIntPoint(-1024, -1024);
	FIntPoint BoundsMaxCell = FIntPoint(1024, 1024);

	/** Service-locator key this subsystem registered itself under, for clean unregister on teardown. */
	UPROPERTY(Transient)
	FGameplayTag RegisteredServiceTag;

	/**
	 * Known chunk carriers keyed by chunk coord. On authority this is the spawn cache; on clients it is
	 * a discovery index. Carriers are owned by the world (level), so we hold WEAK refs — non-owning,
	 * always null-checked before deref.
	 */
	UPROPERTY(Transient)
	TMap<FSimGrid_ChunkCoord, TWeakObjectPtr<ASimGrid_ChunkReplicator>> ChunkCarriers;

	/** Resolve the chunk carrier for a cell (no spawn), or null. */
	ASimGrid_ChunkReplicator* FindCarrierForCell(const FSeam_CellCoord& Cell) const;

	/** Discover any already-replicated carriers in the world (clients) and index them. */
	void IndexExistingCarriers();

	/** Register this subsystem as the read-only tile provider seam (WeakObserved). */
	void RegisterAsTileProvider();
};
