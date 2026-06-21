// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "Grid/Seam_GridCoord.h"
#include "Resource/USurv_ResourceStoreComponent.h"
#include "UBuild_BuildableDefinition.generated.h"

/**
 * Structural role of a buildable in the support graph.
 *
 * Foundations are self-supporting (they anchor the BFS); every other piece must be reachable from a
 * foundation through valid socket connections, or it loses support and (per its rule) may collapse.
 */
UENUM(BlueprintType)
enum class EBuild_StructuralRole : uint8
{
	/** Self-supporting anchor. Roots of the support graph. */
	Foundation,
	/** Vertical support that can carry load (walls, pillars). */
	Support,
	/** Load-bearing horizontal/attached piece that requires an adjacent support/foundation. */
	Attached,
	/** Pure decoration; never collapses and never carries load for others. */
	Decoration
};

/**
 * What happens to a piece when it becomes unsupported.
 */
UENUM(BlueprintType)
enum class EBuild_CollapseBehaviour : uint8
{
	/** Stays in place but is flagged unsupported (cosmetic warning only). */
	Persist,
	/** Is destroyed (removed from the graph and the world) when unsupported. */
	Collapse
};

/**
 * One snap socket: a local-space attachment frame and the socket-type tag it accepts/provides. The
 * placement component snaps a ghost's matching socket to a nearby piece's compatible socket.
 */
USTRUCT(BlueprintType)
struct FBuild_SnapSocket
{
	GENERATED_BODY()

	/** Socket-type tag (child of Surv.Build.SocketType). A socket connects to one of the same type. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|Build")
	FGameplayTag SocketType;

	/** Local-space transform of this socket relative to the piece origin. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|Build")
	FTransform LocalTransform = FTransform::Identity;

	/** When true this socket can provide support to a piece snapped onto it (load-bearing). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|Build")
	bool bProvidesSupport = true;

	FBuild_SnapSocket() = default;
};

/**
 * Data-driven structural rules for a buildable.
 */
USTRUCT(BlueprintType)
struct FBuild_StructuralRule
{
	GENERATED_BODY()

	/** This piece's role in the support graph. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|Build")
	EBuild_StructuralRole Role = EBuild_StructuralRole::Attached;

	/** What happens when this piece loses support. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|Build")
	EBuild_CollapseBehaviour CollapseBehaviour = EBuild_CollapseBehaviour::Collapse;

	/**
	 * Maximum support distance (number of graph hops from a foundation) this piece may sit at. 0 means
	 * "must attach directly to a foundation/support"; higher values allow longer cantilevers. A
	 * defensive default of 8 keeps unbounded structures from being free.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|Build", meta = (ClampMin = "0"))
	int32 MaxSupportDistance = 8;

	FBuild_StructuralRule() = default;
};

/**
 * Pure-data description of a placeable building piece, identified by the core data registry via its
 * inherited DataTag.
 *
 * Carries everything the placement/structure systems need without any behaviour: the spawn-identity
 * tag routed to the core Factory, the grid footprint cells (in piece-local cell space), snap sockets,
 * material cost (reusing the survival resource stack), the structural rule, and an optional tech gate.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSSURVIVAL_API UBuild_BuildableDefinition : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/**
	 * Identity tag routed to UDP_SpawnFactorySubsystem::Spawn to construct the placed actor. The actor
	 * is expected to carry a UBuild_StructureComponent (+ optionally a facility producer/resource store).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Survival|Build")
	FGameplayTag SpawnIdentityTag;

	/**
	 * Footprint cells in piece-LOCAL cell space (relative to the piece's anchor cell). The placement
	 * system rotates + translates these to world cells. Empty = a single-cell footprint at the anchor.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Survival|Build")
	TArray<FSeam_CellCoord> FootprintCells;

	/** Snap sockets used to join this piece to others. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Survival|Build")
	TArray<FBuild_SnapSocket> SnapSockets;

	/** Resource cost paid from the builder's store when committing this piece. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Survival|Build")
	TArray<FSurv_ResourceStack> MaterialCost;

	/** Structural rule (role, collapse behaviour, support distance). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Survival|Build")
	FBuild_StructuralRule Structural;

	/** Tech tag required (via the knowledge ledger) to place this piece. Empty = no tech gate. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Survival|Build")
	FGameplayTag RequiredTechTag;

	/** Effective footprint: returns a single {0,0} cell when FootprintCells is empty. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Survival|Build")
	TArray<FSeam_CellCoord> GetEffectiveLocalFootprint() const
	{
		return FootprintCells.Num() > 0 ? FootprintCells : TArray<FSeam_CellCoord>{ FSeam_CellCoord(0, 0) };
	}

	/** True if this piece is a self-supporting foundation. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Survival|Build")
	bool IsFoundation() const { return Structural.Role == EBuild_StructuralRole::Foundation; }

	/** Groups buildables under one asset-manager bucket for registry enumeration. */
	virtual FName GetDataAssetType_Implementation() const override { return FName(TEXT("Build_Buildable")); }
};
