// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Path/SimGrid_PathQuerySubsystem.h"
#include "Path/SimGrid_PathCacheSubsystem.h"
#include "Settings/SimGrid_FeatureSettings.h"
#include "Settings/SimGrid_DeveloperSettings.h"
#include "Tiles/SimGrid_TileTypeDefinition.h"
#include "Grid/Seam_HeightProvider.h"
#include "SimGrid_NativeTags.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Data/DPDataRegistrySubsystem.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"
#include "Engine/World.h"

//~ Lifecycle -----------------------------------------------------------------------------------

void USimGrid_PathQuerySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// The grid provider is published under the layout settings' service tag (same key the world
	// subsystem registers itself under). Snapshot it so resolution is branch-light.
	if (const USimGrid_DeveloperSettings* Layout = USimGrid_DeveloperSettings::Get())
	{
		GridProviderServiceTag = Layout->TileProviderServiceTag;
	}
	if (!GridProviderServiceTag.IsValid())
	{
		// Fall back to the native anchor so a project that left the settings tag unset still resolves.
		GridProviderServiceTag = SimGridTags::Service_TileProvider;
	}
}

void USimGrid_PathQuerySubsystem::Deinitialize()
{
	CachedGridObject.Reset();
	CachedHeightObject.Reset();
	Super::Deinitialize();
}

//~ Grid / height resolution --------------------------------------------------------------------

TScriptInterface<ISeam_TileProviderRead> USimGrid_PathQuerySubsystem::ResolveGrid() const
{
	TScriptInterface<ISeam_TileProviderRead> Result;

	// Re-use the cached object while it is alive and still implements the seam.
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

	if (!GridProviderServiceTag.IsValid())
	{
		return Result;
	}
	if (const UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		if (UObject* Provider = Locator->ResolveService(GridProviderServiceTag))
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

TScriptInterface<ISeam_HeightProvider> USimGrid_PathQuerySubsystem::ResolveHeightProvider() const
{
	TScriptInterface<ISeam_HeightProvider> Result;

	if (UObject* Cached = CachedHeightObject.Get())
	{
		if (Cached->Implements<USeam_HeightProvider>())
		{
			Result.SetObject(Cached);
			Result.SetInterface(Cast<ISeam_HeightProvider>(Cached));
			return Result;
		}
		CachedHeightObject.Reset();
	}

	const USimGrid_FeatureSettings* Features = USimGrid_FeatureSettings::Get();
	if (!Features || !Features->HeightProviderServiceTag.IsValid())
	{
		return Result;
	}
	if (const UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		if (UObject* Provider = Locator->ResolveService(Features->HeightProviderServiceTag))
		{
			if (Provider->Implements<USeam_HeightProvider>())
			{
				CachedHeightObject = Provider;
				Result.SetObject(Provider);
				Result.SetInterface(Cast<ISeam_HeightProvider>(Provider));
			}
		}
	}
	return Result;
}

//~ Walkability / cost --------------------------------------------------------------------------

bool USimGrid_PathQuerySubsystem::IsCellWalkable(const TScriptInterface<ISeam_TileProviderRead>& Grid,
	const FSeam_CellCoord& Cell) const
{
	if (!Grid)
	{
		return false;
	}
	UObject* GridObj = Grid.GetObject();
	if (!GridObj)
	{
		return false;
	}
	if (!ISeam_TileProviderRead::Execute_IsValidCell(GridObj, Cell))
	{
		return false;
	}

	const FSeam_CellSnapshot Snapshot = ISeam_TileProviderRead::Execute_GetCellSnapshot(GridObj, Cell);

	// Unknown cells are treated as walls so a client never paths across unreplicated regions.
	if (!Snapshot.IsKnown())
	{
		return false;
	}
	// A known-empty cell is walkable ground.
	if (!Snapshot.IsSet())
	{
		return true;
	}

	// A placed tile is walkable only if its definition says so. An unresolved definition is treated as
	// blocking (defensive: an unknown tile type should not be assumed traversable).
	if (UDP_DataRegistrySubsystem* Registry =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_DataRegistrySubsystem>(this))
	{
		if (const USimGrid_TileTypeDefinition* Def =
			Registry->Find<USimGrid_TileTypeDefinition>(Snapshot.TileTypeTag))
		{
			return Def->bWalkable;
		}
	}
	return false;
}

float USimGrid_PathQuerySubsystem::GetCellEntryCost(const TScriptInterface<ISeam_TileProviderRead>& Grid,
	const FSeam_CellCoord& Cell, float DefaultCostOverride) const
{
	const USimGrid_FeatureSettings* Features = USimGrid_FeatureSettings::Get();
	const float SettingsDefault = Features ? Features->GetSafeDefaultTraversalCost() : 1.f;
	const float FallbackCost = (DefaultCostOverride > 0.f) ? DefaultCostOverride : SettingsDefault;

	if (UObject* GridObj = Grid ? Grid.GetObject() : nullptr)
	{
		const FSeam_CellSnapshot Snapshot = ISeam_TileProviderRead::Execute_GetCellSnapshot(GridObj, Cell);
		if (Snapshot.IsSet())
		{
			if (UDP_DataRegistrySubsystem* Registry =
				FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_DataRegistrySubsystem>(this))
			{
				if (const USimGrid_TileTypeDefinition* Def =
					Registry->Find<USimGrid_TileTypeDefinition>(Snapshot.TileTypeTag))
				{
					if (Def->TraversalCost > 0.f)
					{
						return Def->TraversalCost;
					}
				}
			}
		}
	}
	return FallbackCost;
}

bool USimGrid_PathQuerySubsystem::DiagonalAllowed(const TScriptInterface<ISeam_TileProviderRead>& Grid,
	const FSeam_CellCoord& From, const FSeam_CellCoord& To, bool bAllowCornerCut) const
{
	if (bAllowCornerCut)
	{
		return true;
	}
	// No-corner-cut: a diagonal step is legal only if both orthogonal cells it squeezes between are
	// walkable, preventing agents from slipping through the diagonal gap of two blocked cells.
	const FSeam_CellCoord OrthoA(To.X, From.Y);
	const FSeam_CellCoord OrthoB(From.X, To.Y);
	return IsCellWalkable(Grid, OrthoA) && IsCellWalkable(Grid, OrthoB);
}

//~ Pathfinding ---------------------------------------------------------------------------------

namespace
{
	/** One open-set record for the A* search. */
	struct FPathNode
	{
		FSeam_CellCoord Cell;
		float GScore = 0.f; // accumulated cost from start
		float FScore = 0.f; // GScore + heuristic
	};
}

FSimGrid_PathResult USimGrid_PathQuerySubsystem::FindPathOnGrid(
	const TScriptInterface<ISeam_TileProviderRead>& Grid, const FSimGrid_PathRequest& Request) const
{
	FSimGrid_PathResult Out;

	if (!Grid || !Grid.GetObject())
	{
		Out.Result = ESimGrid_PathResult::NoGrid;
		return Out;
	}
	UObject* GridObj = Grid.GetObject();

	if (!ISeam_TileProviderRead::Execute_IsValidCell(GridObj, Request.Start)
		|| !ISeam_TileProviderRead::Execute_IsValidCell(GridObj, Request.Goal))
	{
		Out.Result = ESimGrid_PathResult::OutOfBounds;
		return Out;
	}
	if (!IsCellWalkable(Grid, Request.Start) || !IsCellWalkable(Grid, Request.Goal))
	{
		Out.Result = ESimGrid_PathResult::BlockedEndpoint;
		return Out;
	}
	if (Request.Start == Request.Goal)
	{
		Out.Cells.Add(Request.Start);
		Out.Result = ESimGrid_PathResult::Success;
		return Out;
	}

	const USimGrid_FeatureSettings* Features = USimGrid_FeatureSettings::Get();
	const int32 MaxNodes = Features ? Features->GetSafeMaxPathNodes() : 8192;
	const float DiagMult = Features ? Features->GetSafeDiagonalMultiplier() : 1.41421356f;
	const bool bCornerCut = Features ? Features->bAllowDiagonalCornerCut : false;
	const float SlopePerM = Features ? FMath::Max(0.f, Features->SlopeCostPerMetre) : 0.f;

	const bool bEightWay = (Request.Adjacency == ESimGrid_Adjacency::Eight);
	const TScriptInterface<ISeam_HeightProvider> Height =
		(SlopePerM > 0.f) ? ResolveHeightProvider() : TScriptInterface<ISeam_HeightProvider>();
	UObject* HeightObj = Height ? Height.GetObject() : nullptr;

	// Octile heuristic for 8-way (admissible), Manhattan for 4-way; scaled by the settings default so
	// the heuristic is in the same units as accumulated cost.
	const float HeurUnit = Features ? Features->GetSafeDefaultTraversalCost() : 1.f;
	auto Heuristic = [&](const FSeam_CellCoord& C) -> float
	{
		const int32 DX = FMath::Abs(C.X - Request.Goal.X);
		const int32 DY = FMath::Abs(C.Y - Request.Goal.Y);
		if (bEightWay)
		{
			const int32 DMin = FMath::Min(DX, DY);
			const int32 DMax = FMath::Max(DX, DY);
			return HeurUnit * (static_cast<float>(DMax - DMin) + DiagMult * static_cast<float>(DMin));
		}
		return HeurUnit * static_cast<float>(DX + DY);
	};

	// Open set as a binary min-heap keyed by FScore; closed/best-cost tracked in maps.
	TArray<FPathNode> Open;
	Open.Reserve(64);
	TMap<FSeam_CellCoord, float> BestG;
	TMap<FSeam_CellCoord, FSeam_CellCoord> CameFrom;

	auto NodePred = [](const FPathNode& A, const FPathNode& B) { return A.FScore < B.FScore; };

	FPathNode StartNode{ Request.Start, 0.f, Heuristic(Request.Start) };
	Open.HeapPush(StartNode, NodePred);
	BestG.Add(Request.Start, 0.f);

	int32 Popped = 0;
	TArray<FSeam_CellCoord> Neighbours;
	bool bReached = false;

	while (Open.Num() > 0)
	{
		if (++Popped > MaxNodes)
		{
			Out.Result = ESimGrid_PathResult::NodeBudgetExceeded;
			return Out;
		}

		FPathNode Current;
		Open.HeapPop(Current, NodePred);

		// Skip a stale heap entry whose better cost was already finalised.
		if (const float* Recorded = BestG.Find(Current.Cell))
		{
			if (Current.GScore > *Recorded + KINDA_SMALL_NUMBER)
			{
				continue;
			}
		}

		if (Current.Cell == Request.Goal)
		{
			bReached = true;
			break;
		}

		FSimGrid_CoordMath::GetNeighbours(Current.Cell,
			bEightWay ? ESimGrid_Adjacency::Eight : ESimGrid_Adjacency::Four, Neighbours);

		for (const FSeam_CellCoord& Next : Neighbours)
		{
			if (!IsCellWalkable(Grid, Next))
			{
				continue;
			}
			const bool bDiagonal = (Next.X != Current.Cell.X) && (Next.Y != Current.Cell.Y);
			if (bDiagonal && !DiagonalAllowed(Grid, Current.Cell, Next, bCornerCut))
			{
				continue;
			}

			float StepCost = GetCellEntryCost(Grid, Next, Request.DefaultCostOverride);
			if (bDiagonal)
			{
				StepCost *= DiagMult;
			}

			// Optional uphill slope penalty from the height seam (downhill is free, never negative).
			if (HeightObj)
			{
				const float DeltaCm = ISeam_HeightProvider::Execute_GetHeightDelta(HeightObj, Current.Cell, Next);
				if (DeltaCm > 0.f)
				{
					StepCost += SlopePerM * (DeltaCm / 100.f);
				}
			}

			const float TentativeG = Current.GScore + StepCost;
			const float* Existing = BestG.Find(Next);
			if (!Existing || TentativeG < *Existing - KINDA_SMALL_NUMBER)
			{
				BestG.Add(Next, TentativeG);
				CameFrom.Add(Next, Current.Cell);
				FPathNode NextNode{ Next, TentativeG, TentativeG + Heuristic(Next) };
				Open.HeapPush(NextNode, NodePred);
			}
		}
	}

	++SearchCount;

	if (!bReached)
	{
		Out.Result = ESimGrid_PathResult::NoPath;
		return Out;
	}

	// Reconstruct start..goal by walking CameFrom backwards from the goal, then reverse.
	TArray<FSeam_CellCoord> Reversed;
	FSeam_CellCoord Trace = Request.Goal;
	Reversed.Add(Trace);
	while (Trace != Request.Start)
	{
		const FSeam_CellCoord* Prev = CameFrom.Find(Trace);
		if (!Prev)
		{
			// Defensive: broken parent chain — report no path rather than emit a partial.
			Out.Result = ESimGrid_PathResult::NoPath;
			return Out;
		}
		Trace = *Prev;
		Reversed.Add(Trace);
	}
	Algo::Reverse(Reversed);

	Out.Cells = MoveTemp(Reversed);
	Out.TotalCost = BestG.FindChecked(Request.Goal);
	Out.Result = ESimGrid_PathResult::Success;
	return Out;
}

FSimGrid_PathResult USimGrid_PathQuerySubsystem::FindPath(const FSimGrid_PathRequest& Request)
{
	const TScriptInterface<ISeam_TileProviderRead> Grid = ResolveGrid();
	if (!Grid || !Grid.GetObject())
	{
		FSimGrid_PathResult Out;
		Out.Result = ESimGrid_PathResult::NoGrid;
		return Out;
	}

	// Consult the path cache first; on a hit, return the stored result.
	USimGrid_PathCacheSubsystem* Cache =
		FDP_SubsystemStatics::GetWorldSubsystem<USimGrid_PathCacheSubsystem>(this);
	if (Cache)
	{
		FSimGrid_PathResult Cached;
		if (Cache->TryGetCachedPath(Request, Cached))
		{
			return Cached;
		}
	}

	FSimGrid_PathResult Computed = FindPathOnGrid(Grid, Request);

	if (Cache && Computed.IsValid())
	{
		Cache->StorePath(Request, Computed);
	}
	return Computed;
}

//~ Debug ---------------------------------------------------------------------------------------

FString USimGrid_PathQuerySubsystem::GetDPDebugString_Implementation() const
{
	const bool bHasGrid = ResolveGrid().GetObject() != nullptr;
	return FString::Printf(TEXT("PathQuery: grid=%s searches=%d"),
		bHasGrid ? TEXT("ok") : TEXT("none"), SearchCount);
}
