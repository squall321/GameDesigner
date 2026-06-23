// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Layers/SimGrid_LayerStackSubsystem.h"
#include "World/SimGrid_WorldSubsystem.h"
#include "Settings/SimGrid_FeatureSettings.h"
#include "SimGrid_NativeTags.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"
#include "Engine/World.h"

//~ Lifecycle ---------------------------------------------------------------------------------------

void USimGrid_LayerStackSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Snapshot the configured layer count so runtime reads are branch-light.
	CachedLayerCount = 1;
	if (const USimGrid_FeatureSettings* Settings = USimGrid_FeatureSettings::Get())
	{
		CachedLayerCount = Settings->GetSafeMaxLayerCount();
	}

	// Allocate one map per OVERLAY layer. Layer 0 (base) is not stored here; it is forwarded to
	// USimGrid_WorldSubsystem. So we need (CachedLayerCount - 1) maps.
	const int32 OverlayCount = FMath::Max(0, CachedLayerCount - 1);
	LayerData.SetNum(OverlayCount);

	RegisterAsLayeredProvider();

	UE_LOG(LogDP, Log, TEXT("SimGrid_LayerStackSubsystem: initialized with %d layer(s) (%d overlay), authority=%d."),
		CachedLayerCount, OverlayCount, HasWorldAuthority() ? 1 : 0);
}

void USimGrid_LayerStackSubsystem::Deinitialize()
{
	if (RegisteredServiceTag.IsValid())
	{
		if (UDP_ServiceLocatorSubsystem* Locator =
			FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
		{
			Locator->UnregisterService(RegisteredServiceTag);
		}
	}

	LayerData.Reset();
	Super::Deinitialize();
}

void USimGrid_LayerStackSubsystem::RegisterAsLayeredProvider()
{
	// Prefer the project-configured tag; fall back to the native anchor.
	FGameplayTag ServiceTag = SimGridTags::Service_LayeredTileProvider;
	if (const USimGrid_FeatureSettings* Settings = USimGrid_FeatureSettings::Get())
	{
		if (Settings->LayeredTileProviderServiceTag.IsValid())
		{
			ServiceTag = Settings->LayeredTileProviderServiceTag;
		}
	}

	if (!ServiceTag.IsValid())
	{
		UE_LOG(LogDP, Warning,
			TEXT("SimGrid_LayerStackSubsystem: no valid LayeredTileProvider service tag; not published."));
		return;
	}

	if (UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		// WeakObserved: the locator is GameInstance-scoped and must NOT keep a dead world subsystem alive.
		Locator->RegisterService(ServiceTag, this, EDP_ServiceLifetime::WeakObserved, /*bAllowOverride*/ true);
		RegisteredServiceTag = ServiceTag;
	}
}

//~ Private helpers ---------------------------------------------------------------------------------

USimGrid_WorldSubsystem* USimGrid_LayerStackSubsystem::GetGridSubsystem() const
{
	return FDP_SubsystemStatics::GetWorldSubsystem<USimGrid_WorldSubsystem>(this);
}

bool USimGrid_LayerStackSubsystem::IsLayerInRange(int32 LogicalLayer) const
{
	return LogicalLayer >= 0 && LogicalLayer < CachedLayerCount;
}

//~ Authority mutators ------------------------------------------------------------------------------

bool USimGrid_LayerStackSubsystem::SetLayeredCell(const FSeam_LayeredCellCoord& Coord, const FGameplayTag& TileTypeTag)
{
	// AUTHORITY GUARD — top of every mutator.
	if (!HasWorldAuthority())
	{
		return false;
	}

	if (!TileTypeTag.IsValid())
	{
		return false;
	}

	const int32 LogicalLayer = static_cast<int32>(Coord.Layer);
	if (!IsLayerInRange(LogicalLayer))
	{
		UE_LOG(LogDP, Warning,
			TEXT("SimGrid_LayerStackSubsystem::SetLayeredCell: layer %d out of range [0, %d)."),
			LogicalLayer, CachedLayerCount);
		return false;
	}

	// Layer 0: forward to the flat world subsystem.
	if (LogicalLayer == 0)
	{
		if (USimGrid_WorldSubsystem* GridSub = GetGridSubsystem())
		{
			return GridSub->SetCell(Coord.Cell, TileTypeTag);
		}
		UE_LOG(LogDP, Warning,
			TEXT("SimGrid_LayerStackSubsystem::SetLayeredCell: USimGrid_WorldSubsystem not available for layer 0 write."));
		return false;
	}

	// Overlay layer: store directly. LayerData index = logical layer - 1.
	const int32 DataIndex = LogicalLayer - 1;
	FSeam_CellSnapshot& Snap = LayerData[DataIndex].FindOrAdd(Coord.Cell);
	Snap.KnownState = ESeam_KnownState::Set;
	Snap.TileTypeTag = TileTypeTag;
	return true;
}

bool USimGrid_LayerStackSubsystem::ClearLayeredCell(const FSeam_LayeredCellCoord& Coord)
{
	// AUTHORITY GUARD — top of every mutator.
	if (!HasWorldAuthority())
	{
		return false;
	}

	const int32 LogicalLayer = static_cast<int32>(Coord.Layer);
	if (!IsLayerInRange(LogicalLayer))
	{
		UE_LOG(LogDP, Warning,
			TEXT("SimGrid_LayerStackSubsystem::ClearLayeredCell: layer %d out of range [0, %d)."),
			LogicalLayer, CachedLayerCount);
		return false;
	}

	// Layer 0: forward to the flat world subsystem.
	if (LogicalLayer == 0)
	{
		if (USimGrid_WorldSubsystem* GridSub = GetGridSubsystem())
		{
			return GridSub->ClearCell(Coord.Cell);
		}
		return false;
	}

	// Overlay layer: remove the entry. Returns true if it was present.
	const int32 DataIndex = LogicalLayer - 1;
	return LayerData[DataIndex].Remove(Coord.Cell) > 0;
}

//~ ISeam_LayeredTileProviderRead ------------------------------------------------------------------

int32 USimGrid_LayerStackSubsystem::GetLayerCount_Implementation() const
{
	return CachedLayerCount;
}

FSeam_CellSnapshot USimGrid_LayerStackSubsystem::GetLayeredCellSnapshot_Implementation(
	const FSeam_LayeredCellCoord& Coord) const
{
	FSeam_CellSnapshot Snapshot;

	const int32 LogicalLayer = static_cast<int32>(Coord.Layer);

	// Out-of-range layer: definitively empty (the layer cannot hold a tile).
	if (!IsLayerInRange(LogicalLayer))
	{
		Snapshot.KnownState = ESeam_KnownState::Empty;
		return Snapshot;
	}

	// Layer 0: delegate to the flat world subsystem (tri-state: may return Unknown on clients).
	if (LogicalLayer == 0)
	{
		if (const USimGrid_WorldSubsystem* GridSub = GetGridSubsystem())
		{
			return GridSub->GetCellSnapshot_Implementation(Coord.Cell);
		}
		// No world subsystem available — cannot determine state.
		Snapshot.KnownState = ESeam_KnownState::Unknown;
		return Snapshot;
	}

	// Overlay layer: binary known state (authority-authoritative; no Unknown for stored layers).
	const int32 DataIndex = LogicalLayer - 1;
	if (const FSeam_CellSnapshot* Stored = LayerData[DataIndex].Find(Coord.Cell))
	{
		return *Stored;
	}

	// No entry means the cell is empty on this overlay layer.
	Snapshot.KnownState = ESeam_KnownState::Empty;
	return Snapshot;
}

bool USimGrid_LayerStackSubsystem::IsValidLayeredCell_Implementation(const FSeam_LayeredCellCoord& Coord) const
{
	const int32 LogicalLayer = static_cast<int32>(Coord.Layer);
	if (!IsLayerInRange(LogicalLayer))
	{
		return false;
	}

	// For layer 0 the flat world subsystem validates the cell (bounds check).
	if (LogicalLayer == 0)
	{
		if (const USimGrid_WorldSubsystem* GridSub = GetGridSubsystem())
		{
			return GridSub->IsValidCell_Implementation(Coord.Cell);
		}
		return false;
	}

	// Overlay layers are unbounded unless the base grid bounds it; delegate to the world subsystem
	// for the flat validity check (an overlay cell outside the grid bounds is not useful).
	if (const USimGrid_WorldSubsystem* GridSub = GetGridSubsystem())
	{
		return GridSub->IsValidCell_Implementation(Coord.Cell);
	}
	return true;
}

int32 USimGrid_LayerStackSubsystem::GetBaseLayer_Implementation() const
{
	// Layer 0 is always the base/ground plane, shared with the flat ISeam_TileProviderRead grid.
	return 0;
}

//~ Debug ------------------------------------------------------------------------------------------

FString USimGrid_LayerStackSubsystem::GetDPDebugString_Implementation() const
{
	int32 TotalOverlayCells = 0;
	for (const TMap<FSeam_CellCoord, FSeam_CellSnapshot>& Layer : LayerData)
	{
		TotalOverlayCells += Layer.Num();
	}

	return FString::Printf(TEXT("SimGrid_LayerStack: layers=%d | overlay_cells=%d | authority=%d"),
		CachedLayerCount, TotalOverlayCells, HasWorldAuthority() ? 1 : 0);
}
