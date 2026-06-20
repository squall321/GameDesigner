// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Grid/Seam_GridCoord.h"
#include "Territory/SimGrid_TerritoryTypes.h"
#include "SimGrid_TerritoryComponent.generated.h"

/** Broadcast (server and clients, after replication) whenever cell ownership changes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSimGrid_OnTerritoryChanged, USimGrid_TerritoryComponent*, Territory);

/**
 * Server-authoritative, replicated carrier of grid-cell ownership, exposed as a thin facade.
 *
 * This component is the AUTHORITATIVE home of "who owns which cell". It is meant to live on an AInfo /
 * GameState-side actor (NOT a player), and is the carrier the placement and territory systems mutate.
 * Ownership replicates via a FFastArraySerializer so individual claims/releases delta-replicate. Every
 * mutator (ClaimCells/ReleaseCells/ClaimRegion/...) guards authority at the TOP and early-returns on
 * clients; reads (GetCellOwner/IsOwnedBy) are client-safe and answer from replicated state.
 *
 * It implements ISimGrid_OwnershipRead so rules and other systems query ownership through a seam rather
 * than hard-including this type, and registers itself with the service locator under
 * SimGridTags::Service_TerritoryCarrier so it can be resolved without a hard reference.
 */
UCLASS(ClassGroup = (DesignPatternsSimGrid), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSSIMGRID_API USimGrid_TerritoryComponent : public UActorComponent, public ISimGrid_OwnershipRead
{
	GENERATED_BODY()

public:
	USimGrid_TerritoryComponent();

	//~ Begin UActorComponent
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent

	// --- Authoritative mutators (server only) ---

	/**
	 * Claim every cell in Cells for OwnerId, overwriting any previous owner. AUTHORITY ONLY: early-returns
	 * on clients. Returns the number of cells whose ownership actually changed.
	 */
	UFUNCTION(BlueprintCallable, Category = "SimGrid|Territory")
	int32 ClaimCells(const TArray<FSeam_CellCoord>& Cells, FGameplayTag OwnerId);

	/**
	 * Release every cell in Cells (set to unowned). AUTHORITY ONLY: early-returns on clients. When
	 * bOnlyIfOwnedBy is valid, a cell is released only if currently owned by that identity (so one
	 * owner can't release another's cell). Returns the number of cells actually released.
	 */
	UFUNCTION(BlueprintCallable, Category = "SimGrid|Territory")
	int32 ReleaseCells(const TArray<FSeam_CellCoord>& Cells, FGameplayTag bOnlyIfOwnedBy);

	/** Convenience: claim a single cell. AUTHORITY ONLY. Returns true if ownership changed. */
	UFUNCTION(BlueprintCallable, Category = "SimGrid|Territory")
	bool ClaimCell(const FSeam_CellCoord& Cell, FGameplayTag OwnerId);

	/** Convenience: release a single cell (optionally only if owned by bOnlyIfOwnedBy). AUTHORITY ONLY. */
	UFUNCTION(BlueprintCallable, Category = "SimGrid|Territory")
	bool ReleaseCell(const FSeam_CellCoord& Cell, FGameplayTag bOnlyIfOwnedBy);

	// --- Reads (client-safe) ---

	/** Number of cells currently owned by OwnerId (or total owned cells if OwnerId is invalid). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimGrid|Territory")
	int32 GetOwnedCellCount(FGameplayTag OwnerId) const;

	/** Snapshot of all cells currently owned by OwnerId (read-only copy; client-safe). */
	UFUNCTION(BlueprintCallable, Category = "SimGrid|Territory")
	TArray<FSeam_CellCoord> GetCellsOwnedBy(FGameplayTag OwnerId) const;

	//~ Begin ISimGrid_OwnershipRead
	virtual FGameplayTag GetCellOwner_Implementation(const FSeam_CellCoord& Cell) const override;
	virtual bool IsOwnedBy_Implementation(const FSeam_CellCoord& Cell, const FGameplayTag& OwnerId) const override;
	virtual bool IsOwnershipKnown_Implementation(const FSeam_CellCoord& Cell) const override;
	//~ End ISimGrid_OwnershipRead

	/** Fired (server and clients) after ownership changes. Default broadcasts OnTerritoryChanged. */
	UFUNCTION(BlueprintNativeEvent, Category = "SimGrid|Territory")
	void NotifyTerritoryChanged();
	virtual void NotifyTerritoryChanged_Implementation();

	/** Broadcast whenever ownership changes (after replication on clients). */
	UPROPERTY(BlueprintAssignable, Category = "SimGrid|Territory")
	FSimGrid_OnTerritoryChanged OnTerritoryChanged;

	/** Called by the fast-array entry callbacks on clients to surface an ownership change. */
	void HandleReplicatedChange();

private:
	/** Replicated cell ownership. */
	UPROPERTY(Replicated)
	FSimGrid_OwnershipArray Ownership;

	/**
	 * Server-side acceleration map: cell -> index into Ownership.Entries. Rebuilt from the replicated
	 * array on clients in HandleReplicatedChange and maintained incrementally on the server, so reads
	 * are O(1) without scanning the array. Transient / non-replicated.
	 */
	TMap<FSeam_CellCoord, int32> CellToEntryIndex;

	/** Rebuild CellToEntryIndex from the current Ownership.Entries (after bulk changes / replication). */
	void RebuildIndex();

	/** Find the live entry index for Cell, or INDEX_NONE. */
	int32 FindEntryIndex(const FSeam_CellCoord& Cell) const;

	/** True iff this component's owner has network authority. */
	bool HasOwnerAuthority() const;

	/** Register/unregister this carrier with the service locator under the territory-carrier key. */
	void PublishCarrierService(bool bRegister);
};
