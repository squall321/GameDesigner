// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "Grid/Seam_GridCoord.h"
#include "Identity/Seam_EntityId.h"
#include "UBuild_StructureGraphSubsystem.generated.h"

class UBuild_StructureComponent;

/**
 * Authoritative registry of placed building pieces plus the structural support graph and a
 * cell-occupancy index.
 *
 * - RegisterPiece / UnregisterPiece are AUTHORITY-ONLY effects (no-op without world authority): they
 *   maintain the piece index and cell-occupancy map, then RecomputeSupport.
 * - RecomputeSupport runs a BFS from every foundation along valid socket adjacencies, marking each
 *   reachable piece supported (with a hop distance) and flagging the rest unsupported; pieces whose
 *   rule says Collapse and that are unsupported (or exceed their MaxSupportDistance) are destroyed,
 *   which recursively re-runs the pass for their dependents.
 * - AreCellsFree answers placement-overlap queries for server-side validation.
 *
 * UWorldSubsystem has no HasWorldAuthority(); this subsystem declares its own. Reads are client-safe;
 * mutations require authority.
 */
UCLASS()
class DESIGNPATTERNSSURVIVAL_API UBuild_StructureGraphSubsystem : public UDP_WorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Deinitialize() override;
	//~ End USubsystem

	/**
	 * Register a placed piece. AUTHORITY-ONLY effect. Indexes the piece by id + occupied cells then
	 * recomputes support across the affected component. Safe to call once per piece after spawn.
	 */
	UFUNCTION(BlueprintCallable, Category = "Survival|Build")
	void RegisterPiece(UBuild_StructureComponent* Piece);

	/**
	 * Unregister a piece (removed/destroyed). AUTHORITY-ONLY effect. Removes it from the indexes then
	 * recomputes support, which may cascade-collapse now-unsupported dependents.
	 */
	UFUNCTION(BlueprintCallable, Category = "Survival|Build")
	void UnregisterPiece(UBuild_StructureComponent* Piece);

	/** True if EVERY cell in Cells is currently unoccupied. Client-safe (reads the local index). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Survival|Build")
	bool AreCellsFree(const TArray<FSeam_CellCoord>& Cells) const;

	/** True if EVERY cell in Cells is free EXCEPT those occupied by the piece identified by IgnoreId. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Survival|Build")
	bool AreCellsFreeIgnoring(const TArray<FSeam_CellCoord>& Cells, FSeam_EntityId IgnoreId) const;

	/** Look up a registered piece by id (nullable). Client-safe. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Survival|Build")
	UBuild_StructureComponent* FindPiece(FSeam_EntityId PieceId) const;

	/**
	 * Find the best registered piece whose support-providing socket of type SocketType is nearest to
	 * World within SnapRadius. Used by the placement component to choose a snap parent. Nullable.
	 */
	UFUNCTION(BlueprintCallable, Category = "Survival|Build")
	UBuild_StructureComponent* FindSocketParent(const FVector& World, FGameplayTag SocketType, float SnapRadius) const;

	/**
	 * Recompute the whole support graph from foundations. AUTHORITY-ONLY effect. Marks support flags
	 * and collapses unsupported Collapse-rule pieces (cascading). Idempotent on a stable structure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Survival|Build")
	void RecomputeSupport();

	/** Number of currently-registered pieces. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Survival|Build")
	int32 GetPieceCount() const { return Pieces.Num(); }

	/**
	 * UWorldSubsystem has no HasWorldAuthority(); declare our own. True on the server / standalone /
	 * listen host, false on a pure client.
	 */
	bool HasWorldAuthority() const
	{
		const UWorld* W = GetWorld();
		return W && W->GetNetMode() != NM_Client;
	}

	//~ Begin UDP_WorldSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_WorldSubsystem

private:
	/** Id -> registered piece (weak; pieces are owned by their actors). */
	UPROPERTY(Transient)
	TMap<FSeam_EntityId, TObjectPtr<UBuild_StructureComponent>> Pieces;

	/** Cell -> occupying piece id, for O(1) overlap checks. */
	TMap<FSeam_CellCoord, FSeam_EntityId> CellOwners;

	/** Index a piece's cells into CellOwners (authority-side bookkeeping). */
	void IndexCells(const UBuild_StructureComponent& Piece);

	/** Remove a piece's cells from CellOwners. */
	void DeindexCells(const UBuild_StructureComponent& Piece);

	/** Collapse a piece per its rule (destroy its actor); cascades by recomputing afterward. */
	void CollapsePiece(UBuild_StructureComponent* Piece);

	/** Broadcast a support-changed bus notification for a piece (local; derived from authority state). */
	void NotifySupportChanged(const UBuild_StructureComponent& Piece, bool bSupported) const;
};
