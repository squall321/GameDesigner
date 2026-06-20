// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "GameplayTagContainer.h"
#include "Grid/Seam_GridCoord.h"
#include "Identity/Seam_EntityId.h"
#include "World/SimGrid_CoordTypes.h"
#include "Replication/SimGrid_CellArray.h"

// FInstancedStruct version-gated include (used in ApplyCell payload arguments).
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "SimGrid_ChunkReplicator.generated.h"

/**
 * Fired (server and clients) whenever a single cell's placed state changes on this carrier — after
 * replication on clients. Carries the affected coordinate so listeners can refresh just that cell.
 * @param Carrier The chunk carrier whose cell changed.
 * @param Coord   The affected cell.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSimGrid_OnCellChanged,
	ASimGrid_ChunkReplicator*, Carrier, FSeam_CellCoord, Coord);

/**
 * Fired (server and clients) whenever a single cell's ownership changes on this carrier.
 * @param Carrier The chunk carrier whose ownership changed.
 * @param Coord   The affected cell.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSimGrid_OnOwnershipChanged,
	ASimGrid_ChunkReplicator*, Carrier, FSeam_CellCoord, Coord);

/**
 * Replicated authority carrier for ONE grid chunk.
 *
 * This AInfo owns the chunk's replicated cell array and ownership array. SimGrid subsystems are never
 * replicated; all authoritative grid state lives here. The world subsystem spawns one carrier per
 * occupied chunk on the server and routes every mutation through this actor's authority-guarded
 * mutators. Clients receive cell/ownership deltas via the two FFastArraySerializers and observe
 * changes through the OnCellChanged / OnOwnershipChanged delegates.
 *
 * Net dormancy: the carrier sits DORMANT_Initial and only flushes dormancy when its state actually
 * changes, so an idle chunk costs no per-frame replication bandwidth.
 */
UCLASS()
class DESIGNPATTERNSSIMGRID_API ASimGrid_ChunkReplicator : public AInfo
{
	GENERATED_BODY()

public:
	ASimGrid_ChunkReplicator();

	//~ Begin AActor
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PostInitializeComponents() override;
	//~ End AActor

	/**
	 * Bind this carrier to a chunk coordinate and the chunk size it covers. Server-only, called once
	 * by the world subsystem right after spawn (before the carrier holds any state). Stored values are
	 * replicated so clients know which chunk a received carrier represents.
	 */
	void InitializeChunk(const FSimGrid_ChunkCoord& InChunkCoord, const FIntPoint& InChunkSize);

	/** The chunk coordinate this carrier owns (replicated, set once at spawn). */
	UFUNCTION(BlueprintPure, Category = "SimGrid|Chunk")
	FSimGrid_ChunkCoord GetChunkCoord() const { return ChunkCoord; }

	/** The chunk size (cells per side) this carrier covers (replicated, set once at spawn). */
	UFUNCTION(BlueprintPure, Category = "SimGrid|Chunk")
	FIntPoint GetChunkSize() const { return ChunkSize; }

	// --- Authority mutators (AUTHORITY ONLY; each early-returns on clients) ---

	/**
	 * Place or overwrite a cell with TileTypeTag and an optional payload. AUTHORITY ONLY.
	 * Adds a new entry or updates the existing one, marks it dirty, flushes net dormancy and notifies.
	 * @return True if a change was applied (false on clients, invalid tag, or no-op).
	 */
	bool ApplyCell(const FSeam_CellCoord& Coord, const FGameplayTag& TileTypeTag, const FInstancedStruct& Payload);

	/**
	 * Remove the cell's placed tile (back to empty). AUTHORITY ONLY.
	 * @return True if an entry existed and was removed.
	 */
	bool ClearCell(const FSeam_CellCoord& Coord);

	/**
	 * Claim the given cells for OwnerId, overwriting any prior claim. AUTHORITY ONLY.
	 * @return Number of cells whose ownership actually changed.
	 */
	int32 ClaimCells(const TArray<FSeam_CellCoord>& Coords, const FSeam_EntityId& OwnerId);

	/**
	 * Release any ownership claim on the given cells. AUTHORITY ONLY.
	 * @return Number of cells whose ownership claim was removed.
	 */
	int32 ReleaseCells(const TArray<FSeam_CellCoord>& Coords);

	// --- Reads (safe on clients; observe replicated state) ---

	/** Find the cell entry for Coord, or null. Const, client-safe. */
	const FSimGrid_CellEntry* FindEntry(const FSeam_CellCoord& Coord) const;

	/** Find the ownership entry for Coord, or null. Const, client-safe. */
	const FSimGrid_OwnershipEntry* FindOwnershipEntry(const FSeam_CellCoord& Coord) const;

	/** Read-only access to all cell entries on this carrier. */
	const TArray<FSimGrid_CellEntry>& GetCellEntries() const { return Cells.Entries; }

	/** Read-only access to all ownership entries on this carrier. */
	const TArray<FSimGrid_OwnershipEntry>& GetOwnershipEntries() const { return Ownership.Entries; }

	/** Fired when a cell's placed state changes (server and clients). */
	UPROPERTY(BlueprintAssignable, Category = "SimGrid|Chunk")
	FSimGrid_OnCellChanged OnCellChanged;

	/** Fired when a cell's ownership changes (server and clients). */
	UPROPERTY(BlueprintAssignable, Category = "SimGrid|Chunk")
	FSimGrid_OnOwnershipChanged OnOwnershipChanged;

	/** Called by the cell fast-array item callbacks on clients to surface a replicated cell change. */
	void HandleReplicatedCellChange(const FSeam_CellCoord& Coord);

	/** Called by the ownership fast-array item callbacks on clients to surface a replicated change. */
	void HandleReplicatedOwnershipChange(const FSeam_CellCoord& Coord);

private:
	/** Replicated placed cells for this chunk (delta-serialized). */
	UPROPERTY(Replicated)
	FSimGrid_CellArray Cells;

	/** Replicated ownership claims for this chunk (delta-serialized). */
	UPROPERTY(Replicated)
	FSimGrid_OwnershipArray Ownership;

	/** This carrier's chunk coordinate (replicated, set once at spawn). */
	UPROPERTY(Replicated)
	FSimGrid_ChunkCoord ChunkCoord;

	/** Cell side length covered by this carrier (replicated, set once at spawn). */
	UPROPERTY(Replicated)
	FIntPoint ChunkSize = FIntPoint(16, 16);

	/** Mutable find for the authority mutators; returns null if absent. */
	FSimGrid_CellEntry* FindEntryMutable(const FSeam_CellCoord& Coord);
	FSimGrid_OwnershipEntry* FindOwnershipEntryMutable(const FSeam_CellCoord& Coord);

	/** Wake the actor from net dormancy so a just-changed delta replicates this frame. */
	void WakeForChange();
};
