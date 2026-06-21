// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Grid/Seam_GridCoord.h"
#include "Identity/Seam_EntityId.h"
#include "Identity/Seam_EntityIdentity.h"
#include "Persist/Seam_Persistable.h"
#include "UBuild_StructureComponent.generated.h"

class UBuild_BuildableDefinition;

/**
 * Save record for one placed building piece. Carried through ISeam_Persistable as an FInstancedStruct;
 * SaveGame fields only. The owning actor's world transform is captured separately by the actor-level
 * save path — this record carries the piece's logical state needed to rebuild the graph on load.
 */
USTRUCT()
struct FSurv_StructureSaveRecord
{
	GENERATED_BODY()

	/** The buildable identity this piece was placed from. */
	UPROPERTY(SaveGame)
	FGameplayTag BuildableTag;

	/** This piece's stable id. */
	UPROPERTY(SaveGame)
	FSeam_EntityId PieceId;

	/** Cells this piece occupies (world cell space). */
	UPROPERTY(SaveGame)
	TArray<FSeam_CellCoord> OccupiedCells;

	/** The piece this one draws support from (invalid for foundations). */
	UPROPERTY(SaveGame)
	FSeam_EntityId SupportParentId;

	/** Captured world transform of the owning actor (so a load can respawn it in place). */
	UPROPERTY(SaveGame)
	FTransform WorldTransform = FTransform::Identity;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBuild_OnSupportChanged, bool, bIsSupported);

/**
 * Per-piece replicated state living on a placed building actor.
 *
 * Implements ISeam_EntityIdentity (stable piece id + archetype tag) so the structure graph and the
 * save system key off the id rather than a raw pointer, and ISeam_Persistable so each piece saves
 * its own logical state. The graph subsystem registers/unregisters this component and writes
 * SupportParentId / bIsSupported authoritatively; clients only consume OnRep for cosmetics.
 *
 * OccupiedCells is immutable after placement, so it replicates COND_InitialOnly.
 */
UCLASS(ClassGroup = (DesignPatternsSurvival), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSSURVIVAL_API UBuild_StructureComponent
	: public UActorComponent
	, public ISeam_EntityIdentity
	, public ISeam_Persistable
{
	GENERATED_BODY()

public:
	UBuild_StructureComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent

	/**
	 * Initialize this piece authoritatively right after spawn: assign a fresh id (if unset), record the
	 * buildable tag and occupied cells. AUTHORITY-ONLY. Must be called before RegisterPiece.
	 */
	UFUNCTION(BlueprintCallable, Category = "Survival|Build")
	void InitializePiece(FGameplayTag InBuildableTag, const TArray<FSeam_CellCoord>& InCells, FSeam_EntityId InPieceId);

	/** Set the support parent (graph edge). AUTHORITY-ONLY (called by the graph subsystem). */
	UFUNCTION(BlueprintCallable, Category = "Survival|Build")
	void SetSupportParent(FSeam_EntityId InParentId);

	/** Set the computed support flag. AUTHORITY-ONLY (called by the graph subsystem). */
	UFUNCTION(BlueprintCallable, Category = "Survival|Build")
	void SetSupported(bool bInSupported);

	/** The buildable this piece was placed from. Client-safe. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Survival|Build")
	FGameplayTag GetBuildableTag() const { return BuildableTag; }

	/** This piece's stable id. Client-safe. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Survival|Build")
	FSeam_EntityId GetPieceId() const { return PieceId; }

	/** Cells this piece occupies (world cell space). Client-safe. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Survival|Build")
	const TArray<FSeam_CellCoord>& GetOccupiedCells() const { return OccupiedCells; }

	/** The piece this draws support from (invalid for foundations / unsupported). Client-safe. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Survival|Build")
	FSeam_EntityId GetSupportParentId() const { return SupportParentId; }

	/** True if this piece is currently structurally supported. Client-safe (replicated). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Survival|Build")
	bool IsSupported() const { return bIsSupported; }

	/** Resolve the buildable definition for this piece through the registry (nullable, cached). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Survival|Build")
	UBuild_BuildableDefinition* GetBuildableDef() const;

	// ---- ISeam_EntityIdentity ----
	virtual FSeam_EntityId GetEntityId_Implementation() const override { return PieceId; }
	virtual FGameplayTag GetArchetypeTag_Implementation() const override { return BuildableTag; }

	// ---- ISeam_Persistable ----
	virtual void CaptureState_Implementation(FInstancedStruct& Out) const override;
	virtual void RestoreState_Implementation(const FInstancedStruct& In) override;
	virtual FGameplayTag GetPersistenceKind_Implementation() const override;

	/** Fired (server + clients via OnRep) when this piece gains or loses support. */
	UPROPERTY(BlueprintAssignable, Category = "Survival|Build")
	FBuild_OnSupportChanged OnSupportChanged;

protected:
	/** The buildable identity this piece was placed from. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Survival|Build")
	FGameplayTag BuildableTag;

	/** Stable piece id (net + save). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Survival|Build")
	FSeam_EntityId PieceId;

	/** Cells this piece occupies (world cell space). Immutable after placement => initial-only. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Survival|Build")
	TArray<FSeam_CellCoord> OccupiedCells;

	/** Support parent edge (invalid for foundations). */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Survival|Build")
	FSeam_EntityId SupportParentId;

	/** Computed support flag. */
	UPROPERTY(ReplicatedUsing = OnRep_Supported, BlueprintReadOnly, Category = "Survival|Build")
	bool bIsSupported = true;

	UFUNCTION()
	void OnRep_Supported();

	/** True on the network authority. Gate every mutator on this. */
	bool HasAuthorityToMutate() const;

	/** Lazily-resolved buildable def cache (resolved from registry by BuildableTag). */
	UPROPERTY(Transient)
	mutable TObjectPtr<UBuild_BuildableDefinition> CachedDef;

	/** Register/unregister this piece with the world structure graph subsystem (authority-only effect). */
	void RegisterWithGraph();
	void UnregisterFromGraph();
};
