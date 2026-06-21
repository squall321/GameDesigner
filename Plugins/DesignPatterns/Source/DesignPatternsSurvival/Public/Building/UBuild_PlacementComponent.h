// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Grid/Seam_GridCoord.h"
#include "Identity/Seam_EntityId.h"
#include "UObject/ScriptInterface.h"
#include "UBuild_PlacementComponent.generated.h"

class UBuild_BuildableDefinition;
class USurv_KnowledgeComponent;
class USurv_ResourceStoreComponent;
class ISeam_TileProviderRead;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FBuild_OnPreviewUpdated, FTransform, PreviewTransform, bool, bValid);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBuild_OnPlacementCommitted, FSeam_EntityId, PieceId);

/**
 * Player-owned building placement controller.
 *
 * LOCAL-ONLY preview (the ghost): BeginPreview / UpdatePreview / RotatePreview / IsPlacementValid run
 * purely on the owning client and never mutate authoritative state; they drive a ghost actor / decal
 * via the OnPreviewUpdated delegate. Validation reads the optional ISeam_TileProviderRead grid seam
 * (resolved at runtime, nullable) and the world structure graph for overlap/snap prediction.
 *
 * COMMIT is authority-validated: RequestCommit sends ServerCommitPlacement, which on the server
 * RE-VALIDATES everything (tech gate, materials, footprint free, snap parent), CLAMPS the transform to
 * the authoritative grid cell, pays the material cost, spawns the piece via the core Factory, and
 * registers it with the structure graph. ServerRemovePlacement validates ownership/cost and removes a
 * piece. Both are Server RPCs guarded by WithValidation; the server never trusts the client transform.
 */
UCLASS(ClassGroup = (DesignPatternsSurvival), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSSURVIVAL_API UBuild_PlacementComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBuild_PlacementComponent();

	// ---- Local preview API (owning client only) ----

	/** Begin previewing the buildable identified by BuildableTag. Local; resolves the def + footprint. */
	UFUNCTION(BlueprintCallable, Category = "Survival|Build")
	void BeginPreview(FGameplayTag BuildableTag);

	/** Update the ghost to AimLocation, snapping to the grid + nearby sockets. Local. */
	UFUNCTION(BlueprintCallable, Category = "Survival|Build")
	void UpdatePreview(const FVector& AimLocation);

	/** Rotate the ghost by Steps * 90 degrees about yaw (footprint rotates with it). Local. */
	UFUNCTION(BlueprintCallable, Category = "Survival|Build")
	void RotatePreview(int32 Steps);

	/** Cancel the active preview (clears the ghost). Local. */
	UFUNCTION(BlueprintCallable, Category = "Survival|Build")
	void CancelPreview();

	/** Local prediction of whether the current preview can be placed. OutReason carries a UI message. */
	UFUNCTION(BlueprintCallable, Category = "Survival|Build")
	bool IsPlacementValid(FText& OutReason) const;

	/** Send the current preview to the server for authoritative commit. Owning client only. */
	UFUNCTION(BlueprintCallable, Category = "Survival|Build")
	void RequestCommit();

	/** Current ghost world transform (meaningful only while previewing). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Survival|Build")
	FTransform GetPreviewTransform() const { return PreviewTransform; }

	/** True while a preview is active. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Survival|Build")
	bool IsPreviewing() const { return ActiveBuildableTag.IsValid(); }

	// ---- Authority-validated commit/remove (Server RPCs) ----

	/** Client -> server commit. Server re-validates, clamps to cell, pays cost, spawns, registers. */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerCommitPlacement(FGameplayTag BuildableTag, FTransform PredictedXform, FSeam_EntityId SnapParentId);

	/** Client -> server remove of a placed piece. Server validates and refunds nothing by default. */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerRemovePlacement(FSeam_EntityId PieceId);

	// ---- Config ----

	/** The buildable currently being previewed (also the last requested). */
	UPROPERTY(BlueprintReadOnly, Category = "Survival|Build")
	FGameplayTag ActiveBuildableTag;

	/** Optional knowledge ledger consulted for the per-buildable tech gate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|Build")
	TObjectPtr<USurv_KnowledgeComponent> Knowledge;

	/** Resource store material cost is paid from on commit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|Build")
	TObjectPtr<USurv_ResourceStoreComponent> ResourceStore;

	/** Yaw snap increment in degrees for RotatePreview (defensive default 90; designer-tunable). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|Build", meta = (ClampMin = "1.0", ClampMax = "180.0"))
	float RotationStepDegrees = 90.f;

	/** Socket snap search radius in world units (defensive default; designer-tunable). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|Build", meta = (ClampMin = "0.0"))
	float SnapSearchRadius = 150.f;

	/** Fired (locally) whenever the preview ghost transform / validity changes. */
	UPROPERTY(BlueprintAssignable, Category = "Survival|Build")
	FBuild_OnPreviewUpdated OnPreviewUpdated;

	/** Fired on the server (and replicated effects) when a piece is committed. */
	UPROPERTY(BlueprintAssignable, Category = "Survival|Build")
	FBuild_OnPlacementCommitted OnPlacementCommitted;

protected:
	/** Resolve the buildable definition by tag through the registry (nullable). */
	UBuild_BuildableDefinition* ResolveBuildable(const FGameplayTag& BuildableTag) const;

	/** Resolve the optional read-only grid seam from the world subsystems (nullable). */
	TScriptInterface<ISeam_TileProviderRead> ResolveGridSeam() const;

	/** Current ghost world transform. */
	UPROPERTY(Transient)
	FTransform PreviewTransform = FTransform::Identity;

	/** Current ghost yaw in degrees (accumulated rotation steps). */
	float PreviewYaw = 0.f;

	/** The snap parent piece id chosen during the last UpdatePreview (invalid = freestanding). */
	UPROPERTY(Transient)
	FSeam_EntityId PreviewSnapParentId;

	/** True if the last local validation passed (cached for OnPreviewUpdated broadcasts). */
	bool bLastPreviewValid = false;

	/** True on the network authority. Gate every server mutator on this. */
	bool HasAuthorityToMutate() const;

	/**
	 * Compute the WORLD-space footprint cells for a buildable at a given snap transform via the grid
	 * seam (or a defensive fallback using the def's cell size assumption when no grid is present).
	 */
	TArray<FSeam_CellCoord> ComputeWorldFootprint(
		const UBuild_BuildableDefinition& Def,
		const FTransform& SnapXform,
		const TScriptInterface<ISeam_TileProviderRead>& Grid) const;

	/**
	 * Shared validation used by both the local prediction and the server commit. Resolves the def,
	 * checks the tech gate (via Knowledge), the material cost (via ResourceStore), and footprint
	 * freedom + snap support (via the structure graph). OutWorldCells returns the resolved cells.
	 */
	bool ValidatePlacement(
		const FGameplayTag& BuildableTag,
		const FTransform& SnapXform,
		const FSeam_EntityId& SnapParentId,
		TArray<FSeam_CellCoord>& OutWorldCells,
		FText& OutReason) const;
};
