// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Save/SimGrid_SaveGame.h"
#include "World/SimGrid_WorldSubsystem.h"
#include "Replication/SimGrid_ChunkReplicator.h"
#include "Core/DPLog.h"

bool USimGrid_SaveGame::CaptureFrom(USimGrid_WorldSubsystem* Grid)
{
	if (!Grid)
	{
		UE_LOG(LogDP, Warning, TEXT("SimGrid save CaptureFrom: null world subsystem."));
		return false;
	}
	// Only authority holds the complete grid; a client would capture partial, possibly-stale state.
	if (!Grid->HasWorldAuthority())
	{
		UE_LOG(LogDP, Warning, TEXT("SimGrid save CaptureFrom: refused on non-authority."));
		return false;
	}

	CellSize = Grid->GetCellSize();
	ChunkSize = Grid->GetChunkSize();
	Cells.Reset();
	Ownership.Reset();

	TArray<ASimGrid_ChunkReplicator*> Carriers;
	Grid->GetAllChunkCarriers(Carriers);
	for (const ASimGrid_ChunkReplicator* Carrier : Carriers)
	{
		if (!Carrier)
		{
			continue;
		}
		for (const FSimGrid_CellEntry& Entry : Carrier->GetCellEntries())
		{
			Cells.Emplace(Entry.Coord, Entry.TileTypeTag, Entry.Payload);
		}
		for (const FSimGrid_OwnershipEntry& Owned : Carrier->GetOwnershipEntries())
		{
			Ownership.Emplace(Owned.Coord, Owned.OwnerId);
		}
	}

	UE_LOG(LogDP, Log, TEXT("SimGrid save captured %d cells, %d ownership claims."),
		Cells.Num(), Ownership.Num());
	return true;
}

int32 USimGrid_SaveGame::RestoreInto(USimGrid_WorldSubsystem* Grid) const
{
	if (!Grid)
	{
		UE_LOG(LogDP, Warning, TEXT("SimGrid save RestoreInto: null world subsystem."));
		return 0;
	}
	// Restore routes through authority mutators; refuse on clients so we never fake authoritative state.
	if (!Grid->HasWorldAuthority())
	{
		UE_LOG(LogDP, Warning, TEXT("SimGrid save RestoreInto: refused on non-authority."));
		return 0;
	}

	int32 Applied = 0;
	for (const FSimGrid_SavedCell& Saved : Cells)
	{
		// Routes to carrier ApplyCell (authority-guarded) via the subsystem.
		if (Grid->SetCellWithPayload(Saved.Coord, Saved.TileTypeTag, Saved.Payload))
		{
			++Applied;
		}
	}

	// Re-apply ownership claims, grouped per owner so identical owners batch through ClaimCells.
	TMap<FSeam_EntityId, TArray<FSeam_CellCoord>> ByOwner;
	for (const FSimGrid_SavedOwnership& Owned : Ownership)
	{
		if (Owned.OwnerId.IsValid())
		{
			ByOwner.FindOrAdd(Owned.OwnerId).Add(Owned.Coord);
		}
	}
	for (const TPair<FSeam_EntityId, TArray<FSeam_CellCoord>>& Pair : ByOwner)
	{
		Grid->ClaimCells(Pair.Value, Pair.Key);
	}

	UE_LOG(LogDP, Log, TEXT("SimGrid save restored %d/%d cells, %d ownership groups."),
		Applied, Cells.Num(), ByOwner.Num());
	return Applied;
}

void USimGrid_SaveGame::OnPreSave_Implementation()
{
	Super::OnPreSave_Implementation();
	// Capture is driven explicitly via CaptureFrom (it needs the world subsystem). Nothing extra here.
}

void USimGrid_SaveGame::OnPostLoad_Implementation()
{
	Super::OnPostLoad_Implementation();
	// Scatter is driven explicitly via RestoreInto once the target world subsystem is available.
}
