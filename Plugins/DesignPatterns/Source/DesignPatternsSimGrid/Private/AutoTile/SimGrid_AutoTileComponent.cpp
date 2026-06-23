// Copyright DesignPatterns plugin. All Rights Reserved.

#include "AutoTile/SimGrid_AutoTileComponent.h"
#include "AutoTile/SimGrid_AutoTileLib.h"
#include "AutoTile/SimGrid_AutoTileSet.h"
#include "Settings/SimGrid_DeveloperSettings.h"
#include "Tiles/SimGrid_TileTypeDefinition.h"
#include "World/SimGrid_WorldSubsystem.h"
#include "Replication/SimGrid_ChunkReplicator.h"
#include "SimGrid_NativeTags.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Data/DPDataRegistrySubsystem.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"

USimGrid_AutoTileComponent::USimGrid_AutoTileComponent()
{
	// Cosmetic, event-driven: never ticks, never replicates.
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);
}

void USimGrid_AutoTileComponent::BeginPlay()
{
	Super::BeginPlay();
	BindCarrierDelegates();
	RefreshAll();
}

void USimGrid_AutoTileComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Unbind from carriers so we leave no dangling delegate.
	if (USimGrid_WorldSubsystem* GridWorld = FDP_SubsystemStatics::GetWorldSubsystem<USimGrid_WorldSubsystem>(this))
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
	VisualIndexByCell.Reset();
	Super::EndPlay(EndPlayReason);
}

void USimGrid_AutoTileComponent::BindCarrierDelegates()
{
	USimGrid_WorldSubsystem* GridWorld = FDP_SubsystemStatics::GetWorldSubsystem<USimGrid_WorldSubsystem>(this);
	if (!GridWorld)
	{
		return;
	}
	TArray<ASimGrid_ChunkReplicator*> Carriers;
	GridWorld->GetAllChunkCarriers(Carriers);
	for (ASimGrid_ChunkReplicator* Carrier : Carriers)
	{
		if (Carrier && !Carrier->OnCellChanged.IsAlreadyBound(this, &USimGrid_AutoTileComponent::HandleCarrierCellChanged))
		{
			Carrier->OnCellChanged.AddDynamic(this, &USimGrid_AutoTileComponent::HandleCarrierCellChanged);
		}
	}
}

TScriptInterface<ISeam_TileProviderRead> USimGrid_AutoTileComponent::ResolveGrid() const
{
	TScriptInterface<ISeam_TileProviderRead> Result;

	if (UObject* Cached = CachedGridObject.Get())
	{
		if (Cached->Implements<USeam_TileProviderRead>())
		{
			Result.SetObject(Cached);
			Result.SetInterface(Cast<ISeam_TileProviderRead>(Cached));
			return Result;
		}
		CachedGridObject.Reset();
	}

	FGameplayTag Key = SimGridTags::Service_TileProvider;
	if (const USimGrid_DeveloperSettings* Layout = USimGrid_DeveloperSettings::Get())
	{
		if (Layout->TileProviderServiceTag.IsValid())
		{
			Key = Layout->TileProviderServiceTag;
		}
	}
	if (const UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		if (UObject* Provider = Locator->ResolveService(Key))
		{
			if (Provider->Implements<USeam_TileProviderRead>())
			{
				CachedGridObject = Provider;
				Result.SetObject(Provider);
				Result.SetInterface(Cast<ISeam_TileProviderRead>(Provider));
			}
		}
	}
	return Result;
}

USimGrid_AutoTileSet* USimGrid_AutoTileComponent::FindSetForCategory(const FGameplayTag& Category) const
{
	if (!Category.IsValid())
	{
		return nullptr;
	}
	for (const TObjectPtr<USimGrid_AutoTileSet>& Set : AutoTileSets)
	{
		if (Set && Set->AutoTileCategory.IsValid() && Category.MatchesTag(Set->AutoTileCategory))
		{
			return Set;
		}
	}
	return nullptr;
}

bool USimGrid_AutoTileComponent::InWindow(const FSeam_CellCoord& Cell) const
{
	return Cell.X >= FMath::Min(WindowMin.X, WindowMax.X) && Cell.X <= FMath::Max(WindowMin.X, WindowMax.X)
		&& Cell.Y >= FMath::Min(WindowMin.Y, WindowMax.Y) && Cell.Y <= FMath::Max(WindowMin.Y, WindowMax.Y);
}

void USimGrid_AutoTileComponent::RetileCell(const TScriptInterface<ISeam_TileProviderRead>& Grid, const FSeam_CellCoord& Cell)
{
	UObject* GridObj = Grid ? Grid.GetObject() : nullptr;
	if (!GridObj || !InWindow(Cell))
	{
		return;
	}

	const FSeam_CellSnapshot Snap = ISeam_TileProviderRead::Execute_GetCellSnapshot(GridObj, Cell);

	int32 NewIndex = -1; // empty/unknown cells have no variant
	if (Snap.IsSet())
	{
		// Determine the cell's auto-tile category (definition override, else the exact tile tag).
		FGameplayTag Category = Snap.TileTypeTag;
		if (UDP_DataRegistrySubsystem* Registry =
			FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_DataRegistrySubsystem>(this))
		{
			if (const USimGrid_TileTypeDefinition* Def = Registry->Find<USimGrid_TileTypeDefinition>(Snap.TileTypeTag))
			{
				if (Def->AutoTileCategory.IsValid())
				{
					Category = Def->AutoTileCategory;
				}
			}
		}
		if (USimGrid_AutoTileSet* Set = FindSetForCategory(Category))
		{
			const int32 Mask = USimGrid_AutoTileLib::ComputeAdjacencyBitmask(Grid, Cell, Adjacency);
			NewIndex = Set->ResolveVisualIndex(Mask);
		}
	}

	const int32* Prev = VisualIndexByCell.Find(Cell);
	if (!Prev || *Prev != NewIndex)
	{
		if (NewIndex < 0)
		{
			VisualIndexByCell.Remove(Cell);
		}
		else
		{
			VisualIndexByCell.Add(Cell, NewIndex);
		}
		OnAutoTileVisualChanged.Broadcast(Cell, NewIndex);
	}
}

void USimGrid_AutoTileComponent::RefreshAll()
{
	const TScriptInterface<ISeam_TileProviderRead> Grid = ResolveGrid();
	if (!Grid || !Grid.GetObject())
	{
		return;
	}
	const int32 MinX = FMath::Min(WindowMin.X, WindowMax.X);
	const int32 MaxX = FMath::Max(WindowMin.X, WindowMax.X);
	const int32 MinY = FMath::Min(WindowMin.Y, WindowMax.Y);
	const int32 MaxY = FMath::Max(WindowMin.Y, WindowMax.Y);
	for (int32 Y = MinY; Y <= MaxY; ++Y)
	{
		for (int32 X = MinX; X <= MaxX; ++X)
		{
			RetileCell(Grid, FSeam_CellCoord(X, Y));
		}
	}
}

void USimGrid_AutoTileComponent::HandleCarrierCellChanged(ASimGrid_ChunkReplicator* /*Carrier*/, FSeam_CellCoord Coord)
{
	const TScriptInterface<ISeam_TileProviderRead> Grid = ResolveGrid();
	if (!Grid || !Grid.GetObject())
	{
		return;
	}
	// Re-tile the changed cell plus its 8 neighbours (a change re-bevels adjacent cells).
	RetileCell(Grid, Coord);
	TArray<FSeam_CellCoord> Neighbours;
	FSimGrid_CoordMath::GetNeighbours(Coord, ESimGrid_Adjacency::Eight, Neighbours);
	for (const FSeam_CellCoord& N : Neighbours)
	{
		RetileCell(Grid, N);
	}
}

int32 USimGrid_AutoTileComponent::GetVisualIndex(const FSeam_CellCoord& Cell) const
{
	if (const int32* Found = VisualIndexByCell.Find(Cell))
	{
		return *Found;
	}
	return -1;
}
