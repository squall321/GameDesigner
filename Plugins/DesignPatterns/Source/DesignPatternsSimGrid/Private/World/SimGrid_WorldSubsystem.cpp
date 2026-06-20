// Copyright DesignPatterns plugin. All Rights Reserved.

#include "World/SimGrid_WorldSubsystem.h"
#include "Settings/SimGrid_DeveloperSettings.h"
#include "Replication/SimGrid_ChunkReplicator.h"
#include "Tiles/SimGrid_TileTypeDefinition.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Data/DPDataRegistrySubsystem.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

//~ Lifecycle -----------------------------------------------------------------------------------

void USimGrid_WorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Snapshot settings so per-call reads are branch-light and don't re-hit the CDO.
	if (const USimGrid_DeveloperSettings* Settings = USimGrid_DeveloperSettings::Get())
	{
		CachedCellSize = FMath::Max(1.f, Settings->DefaultCellSize);
		CachedChunkSize = Settings->GetSafeChunkSize();
		bUseBounds = Settings->bUseBounds;
		BoundsMinCell = Settings->BoundsMinCell;
		BoundsMaxCell = Settings->BoundsMaxCell;
		RegisteredServiceTag = Settings->TileProviderServiceTag;
	}

	// Clients may already have replicated carriers; index them so reads resolve immediately.
	IndexExistingCarriers();

	// Publish ourselves as the read-only tile provider seam.
	RegisterAsTileProvider();

	UE_LOG(LogDP, Log, TEXT("SimGrid world subsystem initialized (cell=%.1f, chunk=%dx%d, authority=%d)."),
		CachedCellSize, CachedChunkSize.X, CachedChunkSize.Y, HasWorldAuthority() ? 1 : 0);
}

void USimGrid_WorldSubsystem::Deinitialize()
{
	// Unregister the seam so the locator doesn't observe a dead-world subsystem.
	if (RegisteredServiceTag.IsValid())
	{
		if (UDP_ServiceLocatorSubsystem* Locator =
			FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
		{
			Locator->UnregisterService(RegisteredServiceTag);
		}
	}
	ChunkCarriers.Reset();
	Super::Deinitialize();
}

void USimGrid_WorldSubsystem::RegisterAsTileProvider()
{
	if (!RegisteredServiceTag.IsValid())
	{
		UE_LOG(LogDP, Warning, TEXT("SimGrid: TileProviderServiceTag is unset; grid not published as a seam."));
		return;
	}
	if (UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		// WeakObserved: the locator is GameInstance-scoped and must not keep a dead world's subsystem alive.
		Locator->RegisterService(RegisteredServiceTag, this, EDP_ServiceLifetime::WeakObserved, /*bAllowOverride*/ true);
	}
}

void USimGrid_WorldSubsystem::IndexExistingCarriers()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	for (TActorIterator<ASimGrid_ChunkReplicator> It(World); It; ++It)
	{
		if (ASimGrid_ChunkReplicator* Carrier = *It)
		{
			ChunkCarriers.Add(Carrier->GetChunkCoord(), Carrier);
		}
	}
}

//~ ISeam_TileProviderRead ----------------------------------------------------------------------

FSeam_CellCoord USimGrid_WorldSubsystem::WorldToCell_Implementation(const FVector& WorldLocation) const
{
	const float Size = FMath::Max(1.f, CachedCellSize);
	return FSeam_CellCoord(
		FMath::FloorToInt(static_cast<float>(WorldLocation.X) / Size),
		FMath::FloorToInt(static_cast<float>(WorldLocation.Y) / Size));
}

FVector USimGrid_WorldSubsystem::CellToWorld_Implementation(const FSeam_CellCoord& Cell, bool bCenter) const
{
	const float Size = FMath::Max(1.f, CachedCellSize);
	const float Offset = bCenter ? Size * 0.5f : 0.f;
	return FVector(
		static_cast<double>(Cell.X) * Size + Offset,
		static_cast<double>(Cell.Y) * Size + Offset,
		0.0);
}

bool USimGrid_WorldSubsystem::IsValidCell_Implementation(const FSeam_CellCoord& Cell) const
{
	if (!bUseBounds)
	{
		return true;
	}
	return Cell.X >= BoundsMinCell.X && Cell.X < BoundsMaxCell.X
		&& Cell.Y >= BoundsMinCell.Y && Cell.Y < BoundsMaxCell.Y;
}

FSeam_CellSnapshot USimGrid_WorldSubsystem::GetCellSnapshot_Implementation(const FSeam_CellCoord& Cell) const
{
	FSeam_CellSnapshot Snapshot;

	// Out-of-bounds cells are definitively empty (the grid cannot hold them).
	if (!IsValidCell_Implementation(Cell))
	{
		Snapshot.KnownState = ESeam_KnownState::Empty;
		return Snapshot;
	}

	const ASimGrid_ChunkReplicator* Carrier = FindCarrierForCell(Cell);
	if (!Carrier)
	{
		// No carrier on this machine: on a client the chunk may simply not be replicated yet, so the
		// state is genuinely Unknown. On authority a missing carrier means the chunk is empty.
		Snapshot.KnownState = HasWorldAuthority() ? ESeam_KnownState::Empty : ESeam_KnownState::Unknown;
		return Snapshot;
	}

	if (const FSimGrid_CellEntry* Entry = Carrier->FindEntry(Cell))
	{
		Snapshot.KnownState = ESeam_KnownState::Set;
		Snapshot.TileTypeTag = Entry->TileTypeTag;
	}
	else
	{
		Snapshot.KnownState = ESeam_KnownState::Empty;
	}
	return Snapshot;
}

float USimGrid_WorldSubsystem::GetCellSize_Implementation() const
{
	return CachedCellSize;
}

//~ Carrier resolution --------------------------------------------------------------------------

ASimGrid_ChunkReplicator* USimGrid_WorldSubsystem::FindCarrierForCell(const FSeam_CellCoord& Cell) const
{
	const FSimGrid_ChunkCoord Chunk = FSimGrid_CoordMath::CellToChunk(Cell, CachedChunkSize);
	if (const TWeakObjectPtr<ASimGrid_ChunkReplicator>* Found = ChunkCarriers.Find(Chunk))
	{
		return Found->Get(); // null if the carrier was GC'd; caller treats as Unknown/Empty
	}
	return nullptr;
}

ASimGrid_ChunkReplicator* USimGrid_WorldSubsystem::GetOrSpawnChunkCarrier(const FSimGrid_ChunkCoord& Chunk, bool bSpawnIfMissing)
{
	if (const TWeakObjectPtr<ASimGrid_ChunkReplicator>* Found = ChunkCarriers.Find(Chunk))
	{
		if (ASimGrid_ChunkReplicator* Live = Found->Get())
		{
			return Live;
		}
		// Stale weak entry; drop it before deciding whether to spawn.
		ChunkCarriers.Remove(Chunk);
	}

	if (!bSpawnIfMissing || !HasWorldAuthority())
	{
		return nullptr;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient; // carriers are runtime state restored from saves, not level actors

	ASimGrid_ChunkReplicator* Carrier = World->SpawnActor<ASimGrid_ChunkReplicator>(
		ASimGrid_ChunkReplicator::StaticClass(), FTransform::Identity, Params);
	if (!Carrier)
	{
		UE_LOG(LogDP, Error, TEXT("SimGrid: failed to spawn chunk carrier for %s"), *Chunk.ToString());
		return nullptr;
	}

	Carrier->InitializeChunk(Chunk, CachedChunkSize);
	ChunkCarriers.Add(Chunk, Carrier);
	return Carrier;
}

void USimGrid_WorldSubsystem::GetAllChunkCarriers(TArray<ASimGrid_ChunkReplicator*>& OutCarriers) const
{
	OutCarriers.Reset();
	for (const TPair<FSimGrid_ChunkCoord, TWeakObjectPtr<ASimGrid_ChunkReplicator>>& Pair : ChunkCarriers)
	{
		if (ASimGrid_ChunkReplicator* Live = Pair.Value.Get())
		{
			OutCarriers.Add(Live);
		}
	}
}

//~ Authority mutators (delegate to carriers) ---------------------------------------------------

bool USimGrid_WorldSubsystem::SetCell(const FSeam_CellCoord& Cell, FGameplayTag TileTypeTag)
{
	return SetCellWithPayload(Cell, TileTypeTag, FInstancedStruct());
}

bool USimGrid_WorldSubsystem::SetCellWithPayload(const FSeam_CellCoord& Cell, const FGameplayTag& TileTypeTag,
	const FInstancedStruct& Payload)
{
	// AUTHORITY GUARD at top.
	if (!HasWorldAuthority())
	{
		return false;
	}
	if (!IsValidCell_Implementation(Cell) || !TileTypeTag.IsValid())
	{
		return false;
	}

	ASimGrid_ChunkReplicator* Carrier = GetOrSpawnChunkCarrier(
		FSimGrid_CoordMath::CellToChunk(Cell, CachedChunkSize), /*bSpawnIfMissing*/ true);
	if (!Carrier)
	{
		return false;
	}

	// Seed from the tile type's default payload template when the caller supplied none.
	FInstancedStruct EffectivePayload = Payload;
	if (!EffectivePayload.IsValid())
	{
		if (UDP_DataRegistrySubsystem* Registry =
			FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_DataRegistrySubsystem>(this))
		{
			if (const USimGrid_TileTypeDefinition* Def = Registry->Find<USimGrid_TileTypeDefinition>(TileTypeTag))
			{
				EffectivePayload = Def->DefaultPayloadTemplate;
			}
		}
	}

	return Carrier->ApplyCell(Cell, TileTypeTag, EffectivePayload);
}

bool USimGrid_WorldSubsystem::ClearCell(const FSeam_CellCoord& Cell)
{
	if (!HasWorldAuthority())
	{
		return false;
	}
	ASimGrid_ChunkReplicator* Carrier = GetOrSpawnChunkCarrier(
		FSimGrid_CoordMath::CellToChunk(Cell, CachedChunkSize), /*bSpawnIfMissing*/ false);
	return Carrier ? Carrier->ClearCell(Cell) : false;
}

int32 USimGrid_WorldSubsystem::ClaimCells(const TArray<FSeam_CellCoord>& Cells, const FSeam_EntityId& OwnerId)
{
	if (!HasWorldAuthority())
	{
		return 0;
	}

	// Group cells by chunk so each carrier gets a single batched claim.
	TMap<FSimGrid_ChunkCoord, TArray<FSeam_CellCoord>> ByChunk;
	for (const FSeam_CellCoord& Cell : Cells)
	{
		if (IsValidCell_Implementation(Cell))
		{
			ByChunk.FindOrAdd(FSimGrid_CoordMath::CellToChunk(Cell, CachedChunkSize)).Add(Cell);
		}
	}

	int32 Total = 0;
	for (const TPair<FSimGrid_ChunkCoord, TArray<FSeam_CellCoord>>& Pair : ByChunk)
	{
		if (ASimGrid_ChunkReplicator* Carrier = GetOrSpawnChunkCarrier(Pair.Key, /*bSpawnIfMissing*/ true))
		{
			Total += Carrier->ClaimCells(Pair.Value, OwnerId);
		}
	}
	return Total;
}

int32 USimGrid_WorldSubsystem::ReleaseCells(const TArray<FSeam_CellCoord>& Cells)
{
	if (!HasWorldAuthority())
	{
		return 0;
	}

	TMap<FSimGrid_ChunkCoord, TArray<FSeam_CellCoord>> ByChunk;
	for (const FSeam_CellCoord& Cell : Cells)
	{
		ByChunk.FindOrAdd(FSimGrid_CoordMath::CellToChunk(Cell, CachedChunkSize)).Add(Cell);
	}

	int32 Total = 0;
	for (const TPair<FSimGrid_ChunkCoord, TArray<FSeam_CellCoord>>& Pair : ByChunk)
	{
		if (ASimGrid_ChunkReplicator* Carrier = GetOrSpawnChunkCarrier(Pair.Key, /*bSpawnIfMissing*/ false))
		{
			Total += Carrier->ReleaseCells(Pair.Value);
		}
	}
	return Total;
}

FSeam_EntityId USimGrid_WorldSubsystem::GetCellOwner(const FSeam_CellCoord& Cell) const
{
	if (const ASimGrid_ChunkReplicator* Carrier = FindCarrierForCell(Cell))
	{
		if (const FSimGrid_OwnershipEntry* Entry = Carrier->FindOwnershipEntry(Cell))
		{
			return Entry->OwnerId;
		}
	}
	return FSeam_EntityId::Invalid();
}

//~ Debug ---------------------------------------------------------------------------------------

FString USimGrid_WorldSubsystem::GetDPDebugString_Implementation() const
{
	int32 LiveCarriers = 0;
	int32 TotalCells = 0;
	for (const TPair<FSimGrid_ChunkCoord, TWeakObjectPtr<ASimGrid_ChunkReplicator>>& Pair : ChunkCarriers)
	{
		if (const ASimGrid_ChunkReplicator* Carrier = Pair.Value.Get())
		{
			++LiveCarriers;
			TotalCells += Carrier->GetCellEntries().Num();
		}
	}
	return FString::Printf(TEXT("SimGrid: %s | cell=%.0f chunk=%dx%d | chunks=%d cells=%d"),
		HasWorldAuthority() ? TEXT("AUTHORITY") : TEXT("client"),
		CachedCellSize, CachedChunkSize.X, CachedChunkSize.Y, LiveCarriers, TotalCells);
}
