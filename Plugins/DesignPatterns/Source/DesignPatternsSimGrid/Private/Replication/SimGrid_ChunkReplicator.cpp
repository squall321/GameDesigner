// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Replication/SimGrid_ChunkReplicator.h"
#include "Core/DPLog.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"

//~ FSimGrid_CellEntry fast-array callbacks (client side) ---------------------------------------

void FSimGrid_CellEntry::PreReplicatedRemove(const FSimGrid_CellArray& InArraySerializer)
{
	if (InArraySerializer.OwnerCarrier)
	{
		InArraySerializer.OwnerCarrier->HandleReplicatedCellChange(Coord);
	}
}

void FSimGrid_CellEntry::PostReplicatedAdd(const FSimGrid_CellArray& InArraySerializer)
{
	if (InArraySerializer.OwnerCarrier)
	{
		InArraySerializer.OwnerCarrier->HandleReplicatedCellChange(Coord);
	}
}

void FSimGrid_CellEntry::PostReplicatedChange(const FSimGrid_CellArray& InArraySerializer)
{
	if (InArraySerializer.OwnerCarrier)
	{
		InArraySerializer.OwnerCarrier->HandleReplicatedCellChange(Coord);
	}
}

//~ FSimGrid_OwnershipEntry fast-array callbacks (client side) ----------------------------------

void FSimGrid_OwnershipEntry::PreReplicatedRemove(const FSimGrid_OwnershipArray& InArraySerializer)
{
	if (InArraySerializer.OwnerCarrier)
	{
		InArraySerializer.OwnerCarrier->HandleReplicatedOwnershipChange(Coord);
	}
}

void FSimGrid_OwnershipEntry::PostReplicatedAdd(const FSimGrid_OwnershipArray& InArraySerializer)
{
	if (InArraySerializer.OwnerCarrier)
	{
		InArraySerializer.OwnerCarrier->HandleReplicatedOwnershipChange(Coord);
	}
}

void FSimGrid_OwnershipEntry::PostReplicatedChange(const FSimGrid_OwnershipArray& InArraySerializer)
{
	if (InArraySerializer.OwnerCarrier)
	{
		InArraySerializer.OwnerCarrier->HandleReplicatedOwnershipChange(Coord);
	}
}

//~ ASimGrid_ChunkReplicator --------------------------------------------------------------------

ASimGrid_ChunkReplicator::ASimGrid_ChunkReplicator()
{
	bReplicates = true;
	bAlwaysRelevant = false;
	// Grid chunks rarely change; start dormant and only wake on mutation to save bandwidth.
	NetDormancy = DORM_Initial;
	SetReplicatingMovement(false);
	PrimaryActorTick.bCanEverTick = false;

	// Wire fast-array back-pointers so per-item callbacks can notify us (server and client).
	Cells.OwnerCarrier = this;
	Ownership.OwnerCarrier = this;
}

void ASimGrid_ChunkReplicator::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	// Re-assert back-pointers after any sub-object fixup / net construction.
	Cells.OwnerCarrier = this;
	Ownership.OwnerCarrier = this;
}

void ASimGrid_ChunkReplicator::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASimGrid_ChunkReplicator, Cells);
	DOREPLIFETIME(ASimGrid_ChunkReplicator, Ownership);
	DOREPLIFETIME(ASimGrid_ChunkReplicator, ChunkCoord);
	DOREPLIFETIME(ASimGrid_ChunkReplicator, ChunkSize);
}

void ASimGrid_ChunkReplicator::InitializeChunk(const FSimGrid_ChunkCoord& InChunkCoord, const FIntPoint& InChunkSize)
{
	if (!HasAuthority())
	{
		return;
	}
	ChunkCoord = InChunkCoord;
	ChunkSize = FIntPoint(FMath::Max(1, InChunkSize.X), FMath::Max(1, InChunkSize.Y));
}

void ASimGrid_ChunkReplicator::WakeForChange()
{
	// Move out of dormancy so the just-marked delta is sent, then let the engine re-sleep it.
	if (NetDormancy > DORM_Awake)
	{
		FlushNetDormancy();
	}
}

//~ Reads ---------------------------------------------------------------------------------------

const FSimGrid_CellEntry* ASimGrid_ChunkReplicator::FindEntry(const FSeam_CellCoord& Coord) const
{
	return Cells.Entries.FindByPredicate(
		[&Coord](const FSimGrid_CellEntry& E) { return E.Coord == Coord; });
}

FSimGrid_CellEntry* ASimGrid_ChunkReplicator::FindEntryMutable(const FSeam_CellCoord& Coord)
{
	return Cells.Entries.FindByPredicate(
		[&Coord](const FSimGrid_CellEntry& E) { return E.Coord == Coord; });
}

const FSimGrid_OwnershipEntry* ASimGrid_ChunkReplicator::FindOwnershipEntry(const FSeam_CellCoord& Coord) const
{
	return Ownership.Entries.FindByPredicate(
		[&Coord](const FSimGrid_OwnershipEntry& E) { return E.Coord == Coord; });
}

FSimGrid_OwnershipEntry* ASimGrid_ChunkReplicator::FindOwnershipEntryMutable(const FSeam_CellCoord& Coord)
{
	return Ownership.Entries.FindByPredicate(
		[&Coord](const FSimGrid_OwnershipEntry& E) { return E.Coord == Coord; });
}

//~ Authority mutators --------------------------------------------------------------------------

bool ASimGrid_ChunkReplicator::ApplyCell(const FSeam_CellCoord& Coord, const FGameplayTag& TileTypeTag,
	const FInstancedStruct& Payload)
{
	// AUTHORITY GUARD at top: clients never mutate replicated grid state.
	if (!HasAuthority())
	{
		return false;
	}
	if (!TileTypeTag.IsValid())
	{
		UE_LOG(LogDP, Warning, TEXT("SimGrid ApplyCell rejected: invalid TileTypeTag at %s"), *Coord.ToString());
		return false;
	}

	if (FSimGrid_CellEntry* Existing = FindEntryMutable(Coord))
	{
		// No-op early-out: identical tag and payload means nothing to replicate.
		const bool bSameTag = Existing->TileTypeTag == TileTypeTag;
		const bool bSamePayload = Existing->Payload.Identical(&Payload, /*PortFlags*/ 0);
		if (bSameTag && bSamePayload)
		{
			return false;
		}
		Existing->TileTypeTag = TileTypeTag;
		Existing->Payload = Payload;
		Cells.MarkItemDirty(*Existing);
	}
	else
	{
		FSimGrid_CellEntry& Added = Cells.Entries.Emplace_GetRef(Coord, TileTypeTag);
		Added.Payload = Payload;
		Cells.MarkItemDirty(Added);
	}

	WakeForChange();
	OnCellChanged.Broadcast(this, Coord);
	return true;
}

bool ASimGrid_ChunkReplicator::ClearCell(const FSeam_CellCoord& Coord)
{
	if (!HasAuthority())
	{
		return false;
	}

	const int32 Index = Cells.Entries.IndexOfByPredicate(
		[&Coord](const FSimGrid_CellEntry& E) { return E.Coord == Coord; });
	if (Index == INDEX_NONE)
	{
		return false;
	}

	Cells.Entries.RemoveAt(Index);
	Cells.MarkArrayDirty();
	WakeForChange();
	OnCellChanged.Broadcast(this, Coord);
	return true;
}

int32 ASimGrid_ChunkReplicator::ClaimCells(const TArray<FSeam_CellCoord>& Coords, const FSeam_EntityId& OwnerId)
{
	if (!HasAuthority())
	{
		return 0;
	}
	if (!OwnerId.IsValid())
	{
		UE_LOG(LogDP, Warning, TEXT("SimGrid ClaimCells rejected: invalid OwnerId."));
		return 0;
	}

	int32 Changed = 0;
	for (const FSeam_CellCoord& Coord : Coords)
	{
		if (FSimGrid_OwnershipEntry* Existing = FindOwnershipEntryMutable(Coord))
		{
			if (Existing->OwnerId == OwnerId)
			{
				continue; // already owned by this entity
			}
			Existing->OwnerId = OwnerId;
			Ownership.MarkItemDirty(*Existing);
		}
		else
		{
			FSimGrid_OwnershipEntry& Added = Ownership.Entries.Emplace_GetRef(Coord, OwnerId);
			Ownership.MarkItemDirty(Added);
		}
		OnOwnershipChanged.Broadcast(this, Coord);
		++Changed;
	}

	if (Changed > 0)
	{
		WakeForChange();
	}
	return Changed;
}

int32 ASimGrid_ChunkReplicator::ReleaseCells(const TArray<FSeam_CellCoord>& Coords)
{
	if (!HasAuthority())
	{
		return 0;
	}

	int32 Changed = 0;
	for (const FSeam_CellCoord& Coord : Coords)
	{
		const int32 Index = Ownership.Entries.IndexOfByPredicate(
			[&Coord](const FSimGrid_OwnershipEntry& E) { return E.Coord == Coord; });
		if (Index != INDEX_NONE)
		{
			Ownership.Entries.RemoveAt(Index);
			OnOwnershipChanged.Broadcast(this, Coord);
			++Changed;
		}
	}

	if (Changed > 0)
	{
		Ownership.MarkArrayDirty();
		WakeForChange();
	}
	return Changed;
}

//~ Client-side change surfacing ----------------------------------------------------------------

void ASimGrid_ChunkReplicator::HandleReplicatedCellChange(const FSeam_CellCoord& Coord)
{
	OnCellChanged.Broadcast(this, Coord);
}

void ASimGrid_ChunkReplicator::HandleReplicatedOwnershipChange(const FSeam_CellCoord& Coord)
{
	OnOwnershipChanged.Broadcast(this, Coord);
}
