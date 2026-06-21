// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Building/UBuild_StructureComponent.h"
#include "Building/UBuild_BuildableDefinition.h"
#include "Building/UBuild_StructureGraphSubsystem.h"
#include "DesignPatternsSurvivalTags.h"
#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Data/DPDataRegistrySubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

UBuild_StructureComponent::UBuild_StructureComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UBuild_StructureComponent::BeginPlay()
{
	Super::BeginPlay();
	// Pieces spawned by the placement commit path register explicitly after InitializePiece. A piece
	// that was level-placed (already has an id and cells) self-registers here on authority.
	if (HasAuthorityToMutate() && PieceId.IsValid())
	{
		RegisterWithGraph();
	}
}

void UBuild_StructureComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthorityToMutate())
	{
		UnregisterFromGraph();
	}
	Super::EndPlay(EndPlayReason);
}

void UBuild_StructureComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UBuild_StructureComponent, BuildableTag);
	DOREPLIFETIME(UBuild_StructureComponent, PieceId);
	DOREPLIFETIME_CONDITION(UBuild_StructureComponent, OccupiedCells, COND_InitialOnly);
	DOREPLIFETIME(UBuild_StructureComponent, SupportParentId);
	DOREPLIFETIME(UBuild_StructureComponent, bIsSupported);
}

bool UBuild_StructureComponent::HasAuthorityToMutate() const
{
	return GetOwner() && GetOwner()->HasAuthority();
}

void UBuild_StructureComponent::InitializePiece(FGameplayTag InBuildableTag, const TArray<FSeam_CellCoord>& InCells, FSeam_EntityId InPieceId)
{
	if (!HasAuthorityToMutate())
	{
		return;
	}
	BuildableTag = InBuildableTag;
	OccupiedCells = InCells;
	PieceId = InPieceId.IsValid() ? InPieceId : FSeam_EntityId::NewId();
	CachedDef = nullptr;
}

void UBuild_StructureComponent::SetSupportParent(FSeam_EntityId InParentId)
{
	if (!HasAuthorityToMutate())
	{
		return;
	}
	SupportParentId = InParentId;
}

void UBuild_StructureComponent::SetSupported(bool bInSupported)
{
	if (!HasAuthorityToMutate())
	{
		return;
	}
	if (bIsSupported != bInSupported)
	{
		bIsSupported = bInSupported;
		OnSupportChanged.Broadcast(bIsSupported);
	}
}

UBuild_BuildableDefinition* UBuild_StructureComponent::GetBuildableDef() const
{
	if (CachedDef)
	{
		return CachedDef;
	}
	if (!BuildableTag.IsValid())
	{
		return nullptr;
	}
	if (UDP_DataRegistrySubsystem* Registry =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_DataRegistrySubsystem>(this))
	{
		CachedDef = Registry->Find<UBuild_BuildableDefinition>(BuildableTag);
	}
	return CachedDef;
}

void UBuild_StructureComponent::OnRep_Supported()
{
	OnSupportChanged.Broadcast(bIsSupported);
}

void UBuild_StructureComponent::RegisterWithGraph()
{
	if (UBuild_StructureGraphSubsystem* Graph =
		FDP_SubsystemStatics::GetWorldSubsystem<UBuild_StructureGraphSubsystem>(this))
	{
		Graph->RegisterPiece(this);
	}
}

void UBuild_StructureComponent::UnregisterFromGraph()
{
	if (UBuild_StructureGraphSubsystem* Graph =
		FDP_SubsystemStatics::GetWorldSubsystem<UBuild_StructureGraphSubsystem>(this))
	{
		Graph->UnregisterPiece(this);
	}
}

// ---- ISeam_Persistable ----

void UBuild_StructureComponent::CaptureState_Implementation(FInstancedStruct& Out) const
{
	FSurv_StructureSaveRecord Record;
	Record.BuildableTag = BuildableTag;
	Record.PieceId = PieceId;
	Record.OccupiedCells = OccupiedCells;
	Record.SupportParentId = SupportParentId;
	if (const AActor* Owner = GetOwner())
	{
		Record.WorldTransform = Owner->GetActorTransform();
	}
	Out.InitializeAs<FSurv_StructureSaveRecord>(Record);
}

void UBuild_StructureComponent::RestoreState_Implementation(const FInstancedStruct& In)
{
	if (!HasAuthorityToMutate())
	{
		return; // client-side load is a no-op; server replicates the truth back down
	}
	if (const FSurv_StructureSaveRecord* Record = In.GetPtr<FSurv_StructureSaveRecord>())
	{
		BuildableTag = Record->BuildableTag;
		PieceId = Record->PieceId;
		OccupiedCells = Record->OccupiedCells;
		SupportParentId = Record->SupportParentId;
		CachedDef = nullptr;
		// Re-register with the graph so support is recomputed across the restored set.
		RegisterWithGraph();
	}
}

FGameplayTag UBuild_StructureComponent::GetPersistenceKind_Implementation() const
{
	return SurvNativeTags::Persist_Structure;
}
