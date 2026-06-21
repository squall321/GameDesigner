// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Building/UBuild_StructureGraphSubsystem.h"
#include "Building/UBuild_StructureComponent.h"
#include "Building/UBuild_BuildableDefinition.h"
#include "DesignPatternsSurvivalTags.h"
#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

void UBuild_StructureGraphSubsystem::Deinitialize()
{
	Pieces.Empty();
	CellOwners.Empty();
	Super::Deinitialize();
}

void UBuild_StructureGraphSubsystem::RegisterPiece(UBuild_StructureComponent* Piece)
{
	if (!HasWorldAuthority() || !Piece || !Piece->GetPieceId().IsValid())
	{
		return;
	}
	const FSeam_EntityId Id = Piece->GetPieceId();
	Pieces.Add(Id, Piece);
	IndexCells(*Piece);
	RecomputeSupport();

	UE_LOG(LogDP, Verbose, TEXT("[Survival] Build piece registered: %s (%s)"),
		*Id.ToString(), *Piece->GetBuildableTag().ToString());
}

void UBuild_StructureGraphSubsystem::UnregisterPiece(UBuild_StructureComponent* Piece)
{
	if (!HasWorldAuthority() || !Piece)
	{
		return;
	}
	const FSeam_EntityId Id = Piece->GetPieceId();
	DeindexCells(*Piece);
	Pieces.Remove(Id);
	RecomputeSupport();
}

void UBuild_StructureGraphSubsystem::IndexCells(const UBuild_StructureComponent& Piece)
{
	const FSeam_EntityId Id = Piece.GetPieceId();
	for (const FSeam_CellCoord& Cell : Piece.GetOccupiedCells())
	{
		CellOwners.Add(Cell, Id);
	}
}

void UBuild_StructureGraphSubsystem::DeindexCells(const UBuild_StructureComponent& Piece)
{
	const FSeam_EntityId Id = Piece.GetPieceId();
	for (const FSeam_CellCoord& Cell : Piece.GetOccupiedCells())
	{
		if (const FSeam_EntityId* Owner = CellOwners.Find(Cell))
		{
			if (*Owner == Id)
			{
				CellOwners.Remove(Cell);
			}
		}
	}
}

bool UBuild_StructureGraphSubsystem::AreCellsFree(const TArray<FSeam_CellCoord>& Cells) const
{
	for (const FSeam_CellCoord& Cell : Cells)
	{
		if (CellOwners.Contains(Cell))
		{
			return false;
		}
	}
	return true;
}

bool UBuild_StructureGraphSubsystem::AreCellsFreeIgnoring(const TArray<FSeam_CellCoord>& Cells, FSeam_EntityId IgnoreId) const
{
	for (const FSeam_CellCoord& Cell : Cells)
	{
		if (const FSeam_EntityId* Owner = CellOwners.Find(Cell))
		{
			if (*Owner != IgnoreId)
			{
				return false;
			}
		}
	}
	return true;
}

UBuild_StructureComponent* UBuild_StructureGraphSubsystem::FindPiece(FSeam_EntityId PieceId) const
{
	const TObjectPtr<UBuild_StructureComponent>* Found = Pieces.Find(PieceId);
	return Found ? *Found : nullptr;
}

UBuild_StructureComponent* UBuild_StructureGraphSubsystem::FindSocketParent(const FVector& World, FGameplayTag SocketType, float SnapRadius) const
{
	UBuild_StructureComponent* Best = nullptr;
	float BestDistSq = SnapRadius * SnapRadius;

	for (const TPair<FSeam_EntityId, TObjectPtr<UBuild_StructureComponent>>& Pair : Pieces)
	{
		UBuild_StructureComponent* Piece = Pair.Value;
		if (!Piece || !Piece->GetOwner())
		{
			continue;
		}
		const UBuild_BuildableDefinition* Def = Piece->GetBuildableDef();
		if (!Def)
		{
			continue;
		}
		const FTransform PieceXform = Piece->GetOwner()->GetActorTransform();
		for (const FBuild_SnapSocket& Socket : Def->SnapSockets)
		{
			if (!Socket.bProvidesSupport)
			{
				continue;
			}
			if (SocketType.IsValid() && Socket.SocketType != SocketType)
			{
				continue;
			}
			const FVector SocketWorld = PieceXform.TransformPosition(Socket.LocalTransform.GetLocation());
			const float DistSq = FVector::DistSquared(SocketWorld, World);
			if (DistSq <= BestDistSq)
			{
				BestDistSq = DistSq;
				Best = Piece;
			}
		}
	}
	return Best;
}

void UBuild_StructureGraphSubsystem::RecomputeSupport()
{
	if (!HasWorldAuthority())
	{
		return;
	}

	// BFS from every foundation along support-parent edges. A piece is supported if it is a foundation
	// or it is reachable (within MaxSupportDistance) from a foundation through its support parent.
	TMap<FSeam_EntityId, int32> Distance; // piece id -> hop distance from nearest foundation
	TArray<FSeam_EntityId> Frontier;

	// Seed: foundations are distance 0.
	for (const TPair<FSeam_EntityId, TObjectPtr<UBuild_StructureComponent>>& Pair : Pieces)
	{
		UBuild_StructureComponent* Piece = Pair.Value;
		if (!Piece)
		{
			continue;
		}
		const UBuild_BuildableDefinition* Def = Piece->GetBuildableDef();
		if (Def && Def->IsFoundation())
		{
			Distance.Add(Pair.Key, 0);
			Frontier.Add(Pair.Key);
		}
	}

	// Build a reverse adjacency: parent id -> children that name it as support parent.
	TMap<FSeam_EntityId, TArray<FSeam_EntityId>> Children;
	for (const TPair<FSeam_EntityId, TObjectPtr<UBuild_StructureComponent>>& Pair : Pieces)
	{
		const UBuild_StructureComponent* Piece = Pair.Value;
		if (!Piece)
		{
			continue;
		}
		const FSeam_EntityId Parent = Piece->GetSupportParentId();
		if (Parent.IsValid())
		{
			Children.FindOrAdd(Parent).Add(Pair.Key);
		}
	}

	// Propagate distance outward.
	int32 Head = 0;
	while (Head < Frontier.Num())
	{
		const FSeam_EntityId Current = Frontier[Head++];
		const int32 CurDist = Distance[Current];
		if (const TArray<FSeam_EntityId>* Kids = Children.Find(Current))
		{
			for (const FSeam_EntityId& Kid : *Kids)
			{
				const UBuild_StructureComponent* KidPiece = FindPiece(Kid);
				const UBuild_BuildableDefinition* KidDef = KidPiece ? KidPiece->GetBuildableDef() : nullptr;
				const int32 MaxDist = KidDef ? KidDef->Structural.MaxSupportDistance : 0;
				const int32 NextDist = CurDist + 1;
				if (NextDist <= MaxDist && (!Distance.Contains(Kid) || Distance[Kid] > NextDist))
				{
					Distance.Add(Kid, NextDist);
					Frontier.Add(Kid);
				}
			}
		}
	}

	// Apply support flags and collect collapse candidates.
	TArray<UBuild_StructureComponent*> ToCollapse;
	for (const TPair<FSeam_EntityId, TObjectPtr<UBuild_StructureComponent>>& Pair : Pieces)
	{
		UBuild_StructureComponent* Piece = Pair.Value;
		if (!Piece)
		{
			continue;
		}
		const bool bSupported = Distance.Contains(Pair.Key);
		const bool bWasSupported = Piece->IsSupported();
		Piece->SetSupported(bSupported);
		if (bSupported != bWasSupported)
		{
			NotifySupportChanged(*Piece, bSupported);
		}

		if (!bSupported)
		{
			const UBuild_BuildableDefinition* Def = Piece->GetBuildableDef();
			if (Def && Def->Structural.CollapseBehaviour == EBuild_CollapseBehaviour::Collapse)
			{
				ToCollapse.Add(Piece);
			}
		}
	}

	// Collapse unsupported Collapse-rule pieces. Each collapse destroys an actor, whose EndPlay
	// unregisters it and re-runs RecomputeSupport — cascading to dependents. To avoid reentrant
	// recursion blowing the stack, collapse one piece and let its EndPlay drive the next pass.
	if (ToCollapse.Num() > 0)
	{
		CollapsePiece(ToCollapse[0]);
	}
}

void UBuild_StructureGraphSubsystem::CollapsePiece(UBuild_StructureComponent* Piece)
{
	if (!HasWorldAuthority() || !Piece)
	{
		return;
	}
	AActor* Owner = Piece->GetOwner();
	UE_LOG(LogDP, Verbose, TEXT("[Survival] Build piece collapsing: %s"), *Piece->GetPieceId().ToString());
	NotifySupportChanged(*Piece, false);
	if (Owner)
	{
		// Destroying the actor triggers UBuild_StructureComponent::EndPlay -> UnregisterPiece ->
		// RecomputeSupport, which cascades to any dependents that just lost their parent.
		Owner->Destroy();
	}
}

void UBuild_StructureGraphSubsystem::NotifySupportChanged(const UBuild_StructureComponent& Piece, bool bSupported) const
{
	if (UDP_MessageBusSubsystem* Bus =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		Bus->BroadcastPayload(SurvNativeTags::Bus_BuildSupportChanged, FInstancedStruct(),
			const_cast<UBuild_StructureComponent*>(&Piece));
	}
}

FString UBuild_StructureGraphSubsystem::GetDPDebugString_Implementation() const
{
	return FString::Printf(TEXT("BuildGraph: %d pieces, %d cells, auth=%d"),
		Pieces.Num(), CellOwners.Num(), HasWorldAuthority() ? 1 : 0);
}
