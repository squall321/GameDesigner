// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Building/UBuild_PlacementComponent.h"
#include "Building/UBuild_BuildableDefinition.h"
#include "Building/UBuild_StructureComponent.h"
#include "Building/UBuild_StructureGraphSubsystem.h"
#include "Crafting/USurv_KnowledgeComponent.h"
#include "Resource/USurv_ResourceStoreComponent.h"
#include "DesignPatternsSurvivalTags.h"
#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Data/DPDataRegistrySubsystem.h"
#include "Factory/DPSpawnFactorySubsystem.h"
#include "Factory/DPSpawnRecipe.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Grid/Seam_TileProviderRead.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

#define LOCTEXT_NAMESPACE "Surv_Placement"

UBuild_PlacementComponent::UBuild_PlacementComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	// Player-owned intent component: no replicated state here; intent flows through Server RPCs and the
	// authoritative effect (spawned piece + graph) replicates from the spawned actor.
	SetIsReplicatedByDefault(false);
}

bool UBuild_PlacementComponent::HasAuthorityToMutate() const
{
	return GetOwner() && GetOwner()->HasAuthority();
}

UBuild_BuildableDefinition* UBuild_PlacementComponent::ResolveBuildable(const FGameplayTag& BuildableTag) const
{
	if (!BuildableTag.IsValid())
	{
		return nullptr;
	}
	if (UDP_DataRegistrySubsystem* Registry =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_DataRegistrySubsystem>(this))
	{
		return Registry->Find<UBuild_BuildableDefinition>(BuildableTag);
	}
	return nullptr;
}

TScriptInterface<ISeam_TileProviderRead> UBuild_PlacementComponent::ResolveGridSeam() const
{
	// The grid seam is implemented by an optional world subsystem (SimGrid). We never include the
	// concrete subsystem; instead probe the world's subsystems for one that implements the seam.
	const UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	for (UWorldSubsystem* Sub : World->GetSubsystemArray<UWorldSubsystem>())
	{
		if (Sub && Sub->GetClass()->ImplementsInterface(USeam_TileProviderRead::StaticClass()))
		{
			TScriptInterface<ISeam_TileProviderRead> Result;
			Result.SetObject(Sub);
			Result.SetInterface(Cast<ISeam_TileProviderRead>(Sub));
			return Result;
		}
	}
	return TScriptInterface<ISeam_TileProviderRead>();
}

// ---- Local preview ----

void UBuild_PlacementComponent::BeginPreview(FGameplayTag BuildableTag)
{
	ActiveBuildableTag = BuildableTag;
	PreviewYaw = 0.f;
	PreviewSnapParentId = FSeam_EntityId::Invalid();
	PreviewTransform = FTransform::Identity;
	bLastPreviewValid = false;
}

void UBuild_PlacementComponent::CancelPreview()
{
	ActiveBuildableTag = FGameplayTag();
	PreviewSnapParentId = FSeam_EntityId::Invalid();
	bLastPreviewValid = false;
	OnPreviewUpdated.Broadcast(FTransform::Identity, false);
}

void UBuild_PlacementComponent::RotatePreview(int32 Steps)
{
	PreviewYaw = FMath::UnwindDegrees(PreviewYaw + Steps * RotationStepDegrees);
	// Re-apply yaw to the current preview location.
	UpdatePreview(PreviewTransform.GetLocation());
}

void UBuild_PlacementComponent::UpdatePreview(const FVector& AimLocation)
{
	if (!ActiveBuildableTag.IsValid())
	{
		return;
	}
	const UBuild_BuildableDefinition* Def = ResolveBuildable(ActiveBuildableTag);
	if (!Def)
	{
		bLastPreviewValid = false;
		OnPreviewUpdated.Broadcast(FTransform::Identity, false);
		return;
	}

	const TScriptInterface<ISeam_TileProviderRead> Grid = ResolveGridSeam();

	// Snap location to the grid cell center when a grid is present; otherwise pass through.
	FVector SnappedLocation = AimLocation;
	if (Grid)
	{
		const FSeam_CellCoord Cell = ISeam_TileProviderRead::Execute_WorldToCell(Grid.GetObject(), AimLocation);
		SnappedLocation = ISeam_TileProviderRead::Execute_CellToWorld(Grid.GetObject(), Cell, /*bCenter*/ true);
	}

	const FRotator Rotation(0.f, PreviewYaw, 0.f);
	PreviewTransform = FTransform(Rotation, SnappedLocation);

	// Pick a snap parent (nearest support socket) for support graph wiring on commit.
	PreviewSnapParentId = FSeam_EntityId::Invalid();
	if (UBuild_StructureGraphSubsystem* Graph =
		FDP_SubsystemStatics::GetWorldSubsystem<UBuild_StructureGraphSubsystem>(this))
	{
		FGameplayTag SocketType;
		if (Def->SnapSockets.Num() > 0)
		{
			SocketType = Def->SnapSockets[0].SocketType;
		}
		if (UBuild_StructureComponent* Parent = Graph->FindSocketParent(SnappedLocation, SocketType, SnapSearchRadius))
		{
			PreviewSnapParentId = Parent->GetPieceId();
		}
	}

	FText Reason;
	bLastPreviewValid = IsPlacementValid(Reason);
	OnPreviewUpdated.Broadcast(PreviewTransform, bLastPreviewValid);
}

TArray<FSeam_CellCoord> UBuild_PlacementComponent::ComputeWorldFootprint(
	const UBuild_BuildableDefinition& Def,
	const FTransform& SnapXform,
	const TScriptInterface<ISeam_TileProviderRead>& Grid) const
{
	TArray<FSeam_CellCoord> Local = Def.GetEffectiveLocalFootprint();
	TArray<FSeam_CellCoord> WorldCells;
	WorldCells.Reserve(Local.Num());

	// Yaw determines the integer rotation of local cell offsets (90-degree increments).
	const float Yaw = SnapXform.Rotator().Yaw;
	const int32 QuarterTurns = ((FMath::RoundToInt(Yaw / 90.f) % 4) + 4) % 4;

	auto RotateCell = [QuarterTurns](const FSeam_CellCoord& C) -> FSeam_CellCoord
	{
		switch (QuarterTurns)
		{
		case 1:  return FSeam_CellCoord(-C.Y, C.X);
		case 2:  return FSeam_CellCoord(-C.X, -C.Y);
		case 3:  return FSeam_CellCoord(C.Y, -C.X);
		default: return C;
		}
	};

	// Anchor cell = the snap location's cell (via grid) or a fallback derived from a default cell size.
	FSeam_CellCoord Anchor(0, 0);
	if (Grid)
	{
		Anchor = ISeam_TileProviderRead::Execute_WorldToCell(Grid.GetObject(), SnapXform.GetLocation());
	}
	else
	{
		// Defensive fallback when no grid seam is present: derive a coarse cell from world units using a
		// fixed reference cell size so footprint overlap is still deterministic. Not a gameplay number.
		constexpr float FallbackCellSize = 100.f;
		Anchor = FSeam_CellCoord(
			FMath::FloorToInt(SnapXform.GetLocation().X / FallbackCellSize),
			FMath::FloorToInt(SnapXform.GetLocation().Y / FallbackCellSize));
	}

	for (const FSeam_CellCoord& LocalCell : Local)
	{
		WorldCells.Add(Anchor + RotateCell(LocalCell));
	}
	return WorldCells;
}

bool UBuild_PlacementComponent::ValidatePlacement(
	const FGameplayTag& BuildableTag,
	const FTransform& SnapXform,
	const FSeam_EntityId& SnapParentId,
	TArray<FSeam_CellCoord>& OutWorldCells,
	FText& OutReason) const
{
	OutReason = FText::GetEmpty();
	OutWorldCells.Reset();

	const UBuild_BuildableDefinition* Def = ResolveBuildable(BuildableTag);
	if (!Def)
	{
		OutReason = LOCTEXT("NoBuildable", "Unknown buildable.");
		return false;
	}

	// Tech gate.
	if (Def->RequiredTechTag.IsValid())
	{
		if (!Knowledge || !Knowledge->HasTech(Def->RequiredTechTag))
		{
			OutReason = LOCTEXT("LockedBuild", "Required technology not researched.");
			return false;
		}
	}

	// Material cost (read-only check here; the actual spend happens on the server commit).
	if (ResourceStore)
	{
		for (const FSurv_ResourceStack& Cost : Def->MaterialCost)
		{
			if (!ResourceStore->HasAtLeast(Cost.ItemTag, Cost.Count))
			{
				OutReason = LOCTEXT("NoMaterials", "Insufficient materials.");
				return false;
			}
		}
	}

	// Footprint freedom + snap support via the structure graph.
	const TScriptInterface<ISeam_TileProviderRead> Grid = ResolveGridSeam();
	OutWorldCells = ComputeWorldFootprint(*Def, SnapXform, Grid);

	// If a grid is present, the cells must be valid cells.
	if (Grid)
	{
		for (const FSeam_CellCoord& Cell : OutWorldCells)
		{
			if (!ISeam_TileProviderRead::Execute_IsValidCell(Grid.GetObject(), Cell))
			{
				OutReason = LOCTEXT("OffGrid", "Out of the buildable area.");
				return false;
			}
		}
	}

	if (UBuild_StructureGraphSubsystem* Graph =
		FDP_SubsystemStatics::GetWorldSubsystem<UBuild_StructureGraphSubsystem>(this))
	{
		if (!Graph->AreCellsFree(OutWorldCells))
		{
			OutReason = LOCTEXT("Overlap", "Space is already occupied.");
			return false;
		}
		// Non-foundations must have a valid support parent to attach to.
		if (!Def->IsFoundation() && !SnapParentId.IsValid())
		{
			OutReason = LOCTEXT("NoSupport", "Needs an adjacent support to attach to.");
			return false;
		}
		if (SnapParentId.IsValid() && !Graph->FindPiece(SnapParentId))
		{
			OutReason = LOCTEXT("BadSupport", "Support piece no longer exists.");
			return false;
		}
	}

	return true;
}

bool UBuild_PlacementComponent::IsPlacementValid(FText& OutReason) const
{
	TArray<FSeam_CellCoord> Unused;
	return ValidatePlacement(ActiveBuildableTag, PreviewTransform, PreviewSnapParentId, Unused, OutReason);
}

void UBuild_PlacementComponent::RequestCommit()
{
	if (!ActiveBuildableTag.IsValid())
	{
		return;
	}
	// Predict locally for snappy feedback, then defer to the authoritative commit.
	ServerCommitPlacement(ActiveBuildableTag, PreviewTransform, PreviewSnapParentId);
}

// ---- Server commit / remove ----

void UBuild_PlacementComponent::ServerCommitPlacement_Implementation(FGameplayTag BuildableTag, FTransform PredictedXform, FSeam_EntityId SnapParentId)
{
	if (!HasAuthorityToMutate())
	{
		return;
	}

	// Server re-derives the snapped transform from the grid (never trusts the client transform fully):
	// snap the predicted location to the authoritative cell center, keep yaw quantized to 90 deg.
	const UBuild_BuildableDefinition* Def = ResolveBuildable(BuildableTag);
	if (!Def)
	{
		UE_LOG(LogDP, Warning, TEXT("[Survival] Commit: unknown buildable %s"), *BuildableTag.ToString());
		return;
	}

	FTransform ServerXform = PredictedXform;
	const TScriptInterface<ISeam_TileProviderRead> Grid = ResolveGridSeam();
	if (Grid)
	{
		const FSeam_CellCoord Cell = ISeam_TileProviderRead::Execute_WorldToCell(Grid.GetObject(), PredictedXform.GetLocation());
		const FVector ClampedLoc = ISeam_TileProviderRead::Execute_CellToWorld(Grid.GetObject(), Cell, /*bCenter*/ true);
		const float Yaw = PredictedXform.Rotator().Yaw;
		const float QuantYaw = FMath::RoundToInt(Yaw / 90.f) * 90.f;
		ServerXform = FTransform(FRotator(0.f, QuantYaw, 0.f), ClampedLoc);
	}

	// Authoritative re-validation against the clamped transform.
	TArray<FSeam_CellCoord> WorldCells;
	FText Reason;
	if (!ValidatePlacement(BuildableTag, ServerXform, SnapParentId, WorldCells, Reason))
	{
		UE_LOG(LogDP, Verbose, TEXT("[Survival] Commit rejected %s: %s"),
			*BuildableTag.ToString(), *Reason.ToString());
		return;
	}

	// Pay the material cost (atomic: require everything, then remove).
	if (ResourceStore)
	{
		for (const FSurv_ResourceStack& Cost : Def->MaterialCost)
		{
			if (!ResourceStore->HasAtLeast(Cost.ItemTag, Cost.Count))
			{
				return; // raced out of materials between validate and spend
			}
		}
		for (const FSurv_ResourceStack& Cost : Def->MaterialCost)
		{
			ResourceStore->RemoveResource(Cost.ItemTag, Cost.Count);
		}
	}

	// Spawn the piece through the core Factory.
	UDP_SpawnFactorySubsystem* Factory =
		FDP_SubsystemStatics::GetWorldSubsystem<UDP_SpawnFactorySubsystem>(this);
	if (!Factory || !Def->SpawnIdentityTag.IsValid())
	{
		UE_LOG(LogDP, Warning, TEXT("[Survival] Commit: no factory / spawn identity for %s"), *BuildableTag.ToString());
		return;
	}

	FDP_SpawnParams Params;
	Params.IdentityTag = Def->SpawnIdentityTag;
	Params.Transform = ServerXform;
	Params.Owner = GetOwner();
	Params.bAllowPooling = false; // building pieces are persistent placed actors, not pooled transients

	AActor* Spawned = Factory->Spawn(Def->SpawnIdentityTag, Params);
	if (!Spawned)
	{
		UE_LOG(LogDP, Warning, TEXT("[Survival] Commit: factory failed to spawn %s"), *Def->SpawnIdentityTag.ToString());
		return;
	}

	// Initialize + register the piece's structure component.
	UBuild_StructureComponent* Structure = Spawned->FindComponentByClass<UBuild_StructureComponent>();
	if (!Structure)
	{
		UE_LOG(LogDP, Warning, TEXT("[Survival] Commit: spawned %s has no UBuild_StructureComponent"),
			*Def->SpawnIdentityTag.ToString());
		return;
	}

	const FSeam_EntityId NewId = FSeam_EntityId::NewId();
	Structure->InitializePiece(BuildableTag, WorldCells, NewId);
	Structure->SetSupportParent(SnapParentId);
	if (UBuild_StructureGraphSubsystem* Graph =
		FDP_SubsystemStatics::GetWorldSubsystem<UBuild_StructureGraphSubsystem>(this))
	{
		Graph->RegisterPiece(Structure);
	}

	OnPlacementCommitted.Broadcast(NewId);

	if (UDP_MessageBusSubsystem* Bus =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		Bus->BroadcastPayload(SurvNativeTags::Bus_BuildPlaced, FInstancedStruct(), Spawned);
	}

	UE_LOG(LogDP, Verbose, TEXT("[Survival] Build committed: %s -> %s"),
		*BuildableTag.ToString(), *NewId.ToString());
}

bool UBuild_PlacementComponent::ServerCommitPlacement_Validate(FGameplayTag BuildableTag, FTransform PredictedXform, FSeam_EntityId SnapParentId)
{
	// Reject obviously malformed transforms; full validation is authoritative in _Implementation.
	return BuildableTag.IsValid() && !PredictedXform.ContainsNaN();
}

void UBuild_PlacementComponent::ServerRemovePlacement_Implementation(FSeam_EntityId PieceId)
{
	if (!HasAuthorityToMutate() || !PieceId.IsValid())
	{
		return;
	}
	UBuild_StructureGraphSubsystem* Graph =
		FDP_SubsystemStatics::GetWorldSubsystem<UBuild_StructureGraphSubsystem>(this);
	if (!Graph)
	{
		return;
	}
	UBuild_StructureComponent* Piece = Graph->FindPiece(PieceId);
	if (!Piece || !Piece->GetOwner())
	{
		return;
	}
	AActor* Owner = Piece->GetOwner();
	if (UDP_MessageBusSubsystem* Bus =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		Bus->BroadcastPayload(SurvNativeTags::Bus_BuildRemoved, FInstancedStruct(), Owner);
	}
	// Destroying triggers the structure component's EndPlay -> UnregisterPiece -> RecomputeSupport,
	// which cascade-collapses any dependents that lose support.
	Owner->Destroy();
}

bool UBuild_PlacementComponent::ServerRemovePlacement_Validate(FSeam_EntityId PieceId)
{
	return PieceId.IsValid();
}

#undef LOCTEXT_NAMESPACE
