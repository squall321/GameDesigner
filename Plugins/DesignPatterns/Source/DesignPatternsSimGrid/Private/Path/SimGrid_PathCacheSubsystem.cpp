// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Path/SimGrid_PathCacheSubsystem.h"
#include "Settings/SimGrid_FeatureSettings.h"
#include "World/SimGrid_WorldSubsystem.h"
#include "Replication/SimGrid_ChunkReplicator.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"
#include "Engine/World.h"

void USimGrid_PathCacheSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// Bind to any carriers already present (clients may have replicated carriers before us).
	BindCarrierDelegates();
}

void USimGrid_PathCacheSubsystem::Deinitialize()
{
	// Unbind from every carrier we hooked so we leave no dangling delegate on a surviving carrier.
	if (USimGrid_WorldSubsystem* GridWorld = ResolveGridWorld())
	{
		TArray<ASimGrid_ChunkReplicator*> Carriers;
		GridWorld->GetAllChunkCarriers(Carriers);
		for (ASimGrid_ChunkReplicator* Carrier : Carriers)
		{
			if (Carrier)
			{
				Carrier->OnCellChanged.RemoveAll(this);
			}
		}
	}
	Entries.Empty();
	Super::Deinitialize();
}

USimGrid_WorldSubsystem* USimGrid_PathCacheSubsystem::ResolveGridWorld() const
{
	return FDP_SubsystemStatics::GetWorldSubsystem<USimGrid_WorldSubsystem>(this);
}

void USimGrid_PathCacheSubsystem::BindCarrierDelegates()
{
	USimGrid_WorldSubsystem* GridWorld = ResolveGridWorld();
	if (!GridWorld)
	{
		return;
	}
	TArray<ASimGrid_ChunkReplicator*> Carriers;
	GridWorld->GetAllChunkCarriers(Carriers);
	for (ASimGrid_ChunkReplicator* Carrier : Carriers)
	{
		if (Carrier && !Carrier->OnCellChanged.IsAlreadyBound(this, &USimGrid_PathCacheSubsystem::HandleCarrierCellChanged))
		{
			Carrier->OnCellChanged.AddDynamic(this, &USimGrid_PathCacheSubsystem::HandleCarrierCellChanged);
		}
	}
}

//~ Cache key -----------------------------------------------------------------------------------

USimGrid_PathCacheSubsystem::FPathKey USimGrid_PathCacheSubsystem::MakeKey(const FSimGrid_PathRequest& Request)
{
	FPathKey Key;
	Key.Start = Request.Start;
	Key.Goal = Request.Goal;
	Key.Layer = Request.Layer;
	Key.Adjacency = static_cast<uint8>(Request.Adjacency);
	return Key;
}

//~ Lookup / store ------------------------------------------------------------------------------

bool USimGrid_PathCacheSubsystem::TryGetCachedPath(const FSimGrid_PathRequest& Request, FSimGrid_PathResult& OutResult)
{
	const USimGrid_FeatureSettings* Features = USimGrid_FeatureSettings::Get();
	if (!Features || Features->GetSafePathCacheSize() <= 0)
	{
		return false;
	}

	// Newly spawned carriers may not have been bound yet; pick them up lazily on each lookup.
	BindCarrierDelegates();

	if (FCacheEntry* Entry = Entries.Find(MakeKey(Request)))
	{
		Entry->LastUsedSerial = ++AccessSerial;
		OutResult = Entry->Result;
		return true;
	}
	return false;
}

void USimGrid_PathCacheSubsystem::StorePath(const FSimGrid_PathRequest& Request, const FSimGrid_PathResult& Result)
{
	const USimGrid_FeatureSettings* Features = USimGrid_FeatureSettings::Get();
	const int32 Capacity = Features ? Features->GetSafePathCacheSize() : 0;
	if (Capacity <= 0 || !Result.IsValid())
	{
		return;
	}

	FCacheEntry Entry;
	Entry.Result = Result;
	Entry.OccupiedCells.Reserve(Result.Cells.Num());
	for (const FSeam_CellCoord& C : Result.Cells)
	{
		Entry.OccupiedCells.Add(C);
	}
	Entry.LastUsedSerial = ++AccessSerial;

	Entries.Add(MakeKey(Request), MoveTemp(Entry));
	EnforceCapacity(Capacity);
}

void USimGrid_PathCacheSubsystem::EnforceCapacity(int32 Capacity)
{
	while (Entries.Num() > Capacity)
	{
		// Find and drop the least-recently-used entry.
		const FPathKey* OldestKey = nullptr;
		uint64 OldestSerial = MAX_uint64;
		for (const TPair<FPathKey, FCacheEntry>& Pair : Entries)
		{
			if (Pair.Value.LastUsedSerial < OldestSerial)
			{
				OldestSerial = Pair.Value.LastUsedSerial;
				OldestKey = &Pair.Key;
			}
		}
		if (!OldestKey)
		{
			break;
		}
		Entries.Remove(*OldestKey);
	}
}

//~ Invalidation --------------------------------------------------------------------------------

void USimGrid_PathCacheSubsystem::InvalidateCellPaths(const FSeam_CellCoord& Cell)
{
	if (Entries.Num() == 0)
	{
		return;
	}
	for (auto It = Entries.CreateIterator(); It; ++It)
	{
		if (It->Value.OccupiedCells.Contains(Cell))
		{
			It.RemoveCurrent();
		}
	}
}

void USimGrid_PathCacheSubsystem::InvalidateRegionPaths(const FSeam_CellCoord& Min, const FSeam_CellCoord& Max)
{
	if (Entries.Num() == 0)
	{
		return;
	}
	const int32 MinX = FMath::Min(Min.X, Max.X);
	const int32 MaxX = FMath::Max(Min.X, Max.X);
	const int32 MinY = FMath::Min(Min.Y, Max.Y);
	const int32 MaxY = FMath::Max(Min.Y, Max.Y);

	for (auto It = Entries.CreateIterator(); It; ++It)
	{
		bool bIntersects = false;
		for (const FSeam_CellCoord& C : It->Value.OccupiedCells)
		{
			if (C.X >= MinX && C.X <= MaxX && C.Y >= MinY && C.Y <= MaxY)
			{
				bIntersects = true;
				break;
			}
		}
		if (bIntersects)
		{
			It.RemoveCurrent();
		}
	}
}

void USimGrid_PathCacheSubsystem::ClearCache()
{
	Entries.Empty();
}

//~ Notification sources ------------------------------------------------------------------------

void USimGrid_PathCacheSubsystem::HandleCarrierCellChanged(ASimGrid_ChunkReplicator* /*Carrier*/, FSeam_CellCoord Coord)
{
	InvalidateCellPaths(Coord);
}

void USimGrid_PathCacheSubsystem::OnCellChanged_Implementation(const FSeam_CellCoord& Cell, const FGameplayTag& /*NewTileType*/)
{
	InvalidateCellPaths(Cell);
}

void USimGrid_PathCacheSubsystem::OnRegionChanged_Implementation(const FSeam_CellCoord& Min, const FSeam_CellCoord& Max)
{
	InvalidateRegionPaths(Min, Max);
}

void USimGrid_PathCacheSubsystem::OnCellOwnershipChanged_Implementation(const FSeam_CellCoord& /*Cell*/, const FGameplayTag& /*NewOwnerId*/)
{
	// Ownership does not affect walkability/cost, so cached paths remain valid; intentionally a no-op.
}

//~ Debug ---------------------------------------------------------------------------------------

FString USimGrid_PathCacheSubsystem::GetDPDebugString_Implementation() const
{
	return FString::Printf(TEXT("PathCache: %d entries (serial=%llu)"), Entries.Num(), AccessSerial);
}
