// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Height/SimGrid_HeightSamplerSubsystem.h"
#include "World/SimGrid_WorldSubsystem.h"
#include "Replication/SimGrid_ChunkReplicator.h"
#include "Settings/SimGrid_FeatureSettings.h"
#include "SimGrid_NativeTags.h"
#include "Grid/Seam_TileProviderRead.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"

//~ Lifecycle ---------------------------------------------------------------------------------------

void USimGrid_HeightSamplerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Snapshot trace settings from project config so every read is branch-light.
	if (const USimGrid_FeatureSettings* Settings = USimGrid_FeatureSettings::Get())
	{
		HeightTraceChannel = Settings->HeightTraceChannel;
		HeightTraceStartZ = Settings->HeightTraceStartZ;
		HeightTraceLength = Settings->GetSafeHeightTraceLength();
		HeightFallbackZ = Settings->HeightFallbackZ;
	}

	// Bind to existing carriers so cache invalidation begins immediately on play.
	BindCarrierDelegates();

	RegisterAsHeightProvider();

	UE_LOG(LogDP, Log, TEXT("SimGrid_HeightSamplerSubsystem: initialized. TraceChannel=%d StartZ=%.0f Length=%.0f FallbackZ=%.0f"),
		static_cast<int32>(HeightTraceChannel.GetValue()), HeightTraceStartZ, HeightTraceLength, HeightFallbackZ);
}

void USimGrid_HeightSamplerSubsystem::Deinitialize()
{
	// Unregister from the service locator so a dead-world subsystem is not observed.
	if (RegisteredServiceTag.IsValid())
	{
		if (UDP_ServiceLocatorSubsystem* Locator =
			FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
		{
			Locator->UnregisterService(RegisteredServiceTag);
		}
	}

	// Unbind from all chunk carriers to avoid dangling delegate references.
	if (USimGrid_WorldSubsystem* GridSub = FDP_SubsystemStatics::GetWorldSubsystem<USimGrid_WorldSubsystem>(this))
	{
		TArray<ASimGrid_ChunkReplicator*> Carriers;
		GridSub->GetAllChunkCarriers(Carriers);
		for (ASimGrid_ChunkReplicator* Carrier : Carriers)
		{
			if (Carrier)
			{
				Carrier->OnCellChanged.RemoveAll(this);
			}
		}
	}

	HeightCache.Reset();
	ValidCells.Reset();
	Super::Deinitialize();
}

void USimGrid_HeightSamplerSubsystem::RegisterAsHeightProvider()
{
	// Prefer the project-configured tag; fall back to the native anchor.
	FGameplayTag ServiceTag = SimGridTags::Service_HeightProvider;
	if (const USimGrid_FeatureSettings* Settings = USimGrid_FeatureSettings::Get())
	{
		if (Settings->HeightProviderServiceTag.IsValid())
		{
			ServiceTag = Settings->HeightProviderServiceTag;
		}
	}

	if (!ServiceTag.IsValid())
	{
		UE_LOG(LogDP, Warning,
			TEXT("SimGrid_HeightSamplerSubsystem: no valid HeightProvider service tag; not published."));
		return;
	}

	if (UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		// WeakObserved: the locator is GameInstance-scoped and must not extend a world subsystem's life.
		Locator->RegisterService(ServiceTag, this, EDP_ServiceLifetime::WeakObserved, /*bAllowOverride*/ true);
		RegisteredServiceTag = ServiceTag;
	}
}

void USimGrid_HeightSamplerSubsystem::BindCarrierDelegates()
{
	USimGrid_WorldSubsystem* GridSub = FDP_SubsystemStatics::GetWorldSubsystem<USimGrid_WorldSubsystem>(this);
	if (!GridSub)
	{
		return;
	}

	TArray<ASimGrid_ChunkReplicator*> Carriers;
	GridSub->GetAllChunkCarriers(Carriers);

	for (ASimGrid_ChunkReplicator* Carrier : Carriers)
	{
		if (Carrier && !Carrier->OnCellChanged.IsAlreadyBound(this, &USimGrid_HeightSamplerSubsystem::HandleCarrierCellChanged))
		{
			Carrier->OnCellChanged.AddDynamic(this, &USimGrid_HeightSamplerSubsystem::HandleCarrierCellChanged);
		}
	}
}

//~ Trace helper ------------------------------------------------------------------------------------

float USimGrid_HeightSamplerSubsystem::RunHeightTrace(const FSeam_CellCoord& Cell, bool& bOutValid) const
{
	bOutValid = false;

	UWorld* World = GetWorld();
	if (!World)
	{
		return HeightFallbackZ;
	}

	// Resolve the flat grid provider for CellToWorld (cell-centre XY).
	const USimGrid_WorldSubsystem* GridSub = FDP_SubsystemStatics::GetWorldSubsystem<USimGrid_WorldSubsystem>(this);
	if (!GridSub)
	{
		return HeightFallbackZ;
	}

	// Get the cell's centre world position (bCenter=true) and keep only XY.
	const FVector CellCentre = ISeam_TileProviderRead::Execute_CellToWorld(
		const_cast<USimGrid_WorldSubsystem*>(GridSub), Cell, /*bCenter*/ true);

	const FVector TraceStart(CellCentre.X, CellCentre.Y, static_cast<double>(HeightTraceStartZ));
	const FVector TraceEnd(CellCentre.X, CellCentre.Y, static_cast<double>(HeightTraceStartZ - HeightTraceLength));

	FHitResult HitResult;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SimGridHeightTrace), /*bTraceComplex*/ false);
	QueryParams.bReturnFaceIndex = false;

	if (World->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, HeightTraceChannel, QueryParams))
	{
		bOutValid = true;
		return static_cast<float>(HitResult.ImpactPoint.Z);
	}

	return HeightFallbackZ;
}

//~ Public API --------------------------------------------------------------------------------------

float USimGrid_HeightSamplerSubsystem::SampleCellHeightNow(const FSeam_CellCoord& Cell) const
{
	bool bValid = false;
	return RunHeightTrace(Cell, bValid);
}

void USimGrid_HeightSamplerSubsystem::InvalidateCell(const FSeam_CellCoord& Cell)
{
	HeightCache.Remove(Cell);
	ValidCells.Remove(Cell);
}

void USimGrid_HeightSamplerSubsystem::ClearCache()
{
	HeightCache.Reset();
	ValidCells.Reset();
}

//~ ISeam_HeightProvider ---------------------------------------------------------------------------

float USimGrid_HeightSamplerSubsystem::SampleCellHeight_Implementation(
	const FSeam_CellCoord& Cell, bool& bOutValid) const
{
	// Check the cache first.
	if (const float* Cached = HeightCache.Find(Cell))
	{
		bOutValid = ValidCells.Contains(Cell);
		return *Cached;
	}

	// Cache miss: run a trace, populate the cache, and return the result.
	bool bTraceValid = false;
	const float Height = RunHeightTrace(Cell, bTraceValid);

	HeightCache.Add(Cell, Height);
	if (bTraceValid)
	{
		ValidCells.Add(Cell);
	}

	bOutValid = bTraceValid;
	return Height;
}

float USimGrid_HeightSamplerSubsystem::GetHeightDelta_Implementation(
	const FSeam_CellCoord& A, const FSeam_CellCoord& B) const
{
	bool bValidA = false;
	bool bValidB = false;
	const float HeightA = SampleCellHeight_Implementation(A, bValidA);
	const float HeightB = SampleCellHeight_Implementation(B, bValidB);

	// If either cell has no valid height, the delta is meaningless; return 0 (flat).
	if (!bValidA || !bValidB)
	{
		return 0.f;
	}

	return HeightB - HeightA;
}

bool USimGrid_HeightSamplerSubsystem::HasSampledHeight_Implementation(const FSeam_CellCoord& Cell) const
{
	// A "sampled" height is a valid (non-fallback) trace result stored in ValidCells.
	return ValidCells.Contains(Cell);
}

//~ ISimGrid_GridObserver ---------------------------------------------------------------------------

void USimGrid_HeightSamplerSubsystem::OnCellChanged_Implementation(
	const FSeam_CellCoord& Cell, const FGameplayTag& /*NewTileType*/)
{
	// A tile change can alter the surface geometry visible from above (e.g. a building placed/removed).
	InvalidateCell(Cell);
}

void USimGrid_HeightSamplerSubsystem::OnRegionChanged_Implementation(
	const FSeam_CellCoord& Min, const FSeam_CellCoord& Max)
{
	// Evict every cell in the changed region.
	const int32 MinX = FMath::Min(Min.X, Max.X);
	const int32 MaxX = FMath::Max(Min.X, Max.X);
	const int32 MinY = FMath::Min(Min.Y, Max.Y);
	const int32 MaxY = FMath::Max(Min.Y, Max.Y);

	for (int32 Y = MinY; Y <= MaxY; ++Y)
	{
		for (int32 X = MinX; X <= MaxX; ++X)
		{
			const FSeam_CellCoord Cell(X, Y);
			HeightCache.Remove(Cell);
			ValidCells.Remove(Cell);
		}
	}
}

void USimGrid_HeightSamplerSubsystem::OnCellOwnershipChanged_Implementation(
	const FSeam_CellCoord& /*Cell*/, const FGameplayTag& /*NewOwnerId*/)
{
	// Height is terrain geometry — ownership changes do not affect it. No-op.
}

//~ Carrier delegate handler -----------------------------------------------------------------------

void USimGrid_HeightSamplerSubsystem::HandleCarrierCellChanged(
	ASimGrid_ChunkReplicator* /*Carrier*/, FSeam_CellCoord Coord)
{
	// Route through the observer interface so all cache-invalidation logic lives in one place.
	OnCellChanged_Implementation(Coord, FGameplayTag::EmptyTag);
}

//~ Debug ------------------------------------------------------------------------------------------

FString USimGrid_HeightSamplerSubsystem::GetDPDebugString_Implementation() const
{
	return FString::Printf(TEXT("SimGrid_HeightSampler: cache_size=%d valid=%d"),
		HeightCache.Num(), ValidCells.Num());
}
