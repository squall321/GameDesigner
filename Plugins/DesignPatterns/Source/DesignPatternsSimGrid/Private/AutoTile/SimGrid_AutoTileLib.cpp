// Copyright DesignPatterns plugin. All Rights Reserved.

#include "AutoTile/SimGrid_AutoTileLib.h"
#include "Settings/SimGrid_FeatureSettings.h"
#include "Tiles/SimGrid_TileTypeDefinition.h"
#include "Data/DPDataRegistrySubsystem.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"

namespace
{
	/** Resolve a Set cell's tile definition (or null for empty/unknown/unresolved). */
	const USimGrid_TileTypeDefinition* ResolveTileDef(const UObject* WorldContext,
		const FSeam_CellSnapshot& Snapshot)
	{
		if (!Snapshot.IsSet())
		{
			return nullptr;
		}
		if (UDP_DataRegistrySubsystem* Registry =
			FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_DataRegistrySubsystem>(WorldContext))
		{
			return Registry->Find<USimGrid_TileTypeDefinition>(Snapshot.TileTypeTag);
		}
		return nullptr;
	}

	/** The auto-tile category of a Set cell: its definition's AutoTileCategory, else its exact tile tag. */
	FGameplayTag CellCategory(const UObject* WorldContext, const FSeam_CellSnapshot& Snapshot)
	{
		if (!Snapshot.IsSet())
		{
			return FGameplayTag();
		}
		if (const USimGrid_TileTypeDefinition* Def = ResolveTileDef(WorldContext, Snapshot))
		{
			if (Def->AutoTileCategory.IsValid())
			{
				return Def->AutoTileCategory;
			}
		}
		return Snapshot.TileTypeTag;
	}

	/** True if a Set cell matches MatchCategory (or, when MatchCategory is invalid, just being Set). */
	bool CellMatches(const UObject* WorldContext, const FSeam_CellSnapshot& Snapshot, const FGameplayTag& MatchCategory)
	{
		if (!Snapshot.IsSet())
		{
			return false;
		}
		if (!MatchCategory.IsValid())
		{
			return true; // any Set cell when no category filter is given
		}
		return CellCategory(WorldContext, Snapshot).MatchesTag(MatchCategory);
	}
}

bool USimGrid_AutoTileLib::ConnectsTo(const TScriptInterface<ISeam_TileProviderRead>& Grid,
	const FSeam_CellCoord& Cell, const FSeam_CellCoord& Neighbour)
{
	UObject* GridObj = Grid ? Grid.GetObject() : nullptr;
	if (!GridObj)
	{
		return false;
	}
	const FSeam_CellSnapshot SelfSnap = ISeam_TileProviderRead::Execute_GetCellSnapshot(GridObj, Cell);
	const FSeam_CellSnapshot NeighSnap = ISeam_TileProviderRead::Execute_GetCellSnapshot(GridObj, Neighbour);
	if (!SelfSnap.IsSet() || !NeighSnap.IsSet())
	{
		return false; // Unknown / Empty cells never connect
	}

	const FGameplayTag SelfCat = CellCategory(GridObj, SelfSnap);
	const FGameplayTag NeighCat = CellCategory(GridObj, NeighSnap);
	// Connected when categories match (covers both the shared-category and exact-tag-equality cases,
	// since CellCategory falls back to the exact tag when no category is authored).
	return SelfCat.IsValid() && SelfCat == NeighCat;
}

int32 USimGrid_AutoTileLib::ComputeAdjacencyBitmask(const TScriptInterface<ISeam_TileProviderRead>& Grid,
	const FSeam_CellCoord& Cell, ESimGrid_Adjacency Adjacency)
{
	if (!Grid || !Grid.GetObject())
	{
		return 0;
	}

	// Cardinal bits: +X=1, -X=2, +Y=4, -Y=8.
	const bool bPX = ConnectsTo(Grid, Cell, FSeam_CellCoord(Cell.X + 1, Cell.Y));
	const bool bNX = ConnectsTo(Grid, Cell, FSeam_CellCoord(Cell.X - 1, Cell.Y));
	const bool bPY = ConnectsTo(Grid, Cell, FSeam_CellCoord(Cell.X, Cell.Y + 1));
	const bool bNY = ConnectsTo(Grid, Cell, FSeam_CellCoord(Cell.X, Cell.Y - 1));

	int32 Mask = 0;
	Mask |= bPX ? 1 : 0;
	Mask |= bNX ? 2 : 0;
	Mask |= bPY ? 4 : 0;
	Mask |= bNY ? 8 : 0;

	if (Adjacency == ESimGrid_Adjacency::Eight)
	{
		// Diagonal bits +X+Y=16, +X-Y=32, -X+Y=64, -X-Y=128, set only when BOTH flanking cardinals
		// connect (the blob rule that yields the reduced 47-tile wang set).
		const bool bPXPY = bPX && bPY && ConnectsTo(Grid, Cell, FSeam_CellCoord(Cell.X + 1, Cell.Y + 1));
		const bool bPXNY = bPX && bNY && ConnectsTo(Grid, Cell, FSeam_CellCoord(Cell.X + 1, Cell.Y - 1));
		const bool bNXPY = bNX && bPY && ConnectsTo(Grid, Cell, FSeam_CellCoord(Cell.X - 1, Cell.Y + 1));
		const bool bNXNY = bNX && bNY && ConnectsTo(Grid, Cell, FSeam_CellCoord(Cell.X - 1, Cell.Y - 1));
		Mask |= bPXPY ? 16 : 0;
		Mask |= bPXNY ? 32 : 0;
		Mask |= bNXPY ? 64 : 0;
		Mask |= bNXNY ? 128 : 0;
	}
	return Mask;
}

FSimGrid_RegionLabeling USimGrid_AutoTileLib::LabelConnectedRegions(
	const TScriptInterface<ISeam_TileProviderRead>& Grid, const FSeam_CellCoord& Min, const FSeam_CellCoord& Max,
	FGameplayTag MatchCategory, ESimGrid_Adjacency Adjacency)
{
	FSimGrid_RegionLabeling Out;

	UObject* GridObj = Grid ? Grid.GetObject() : nullptr;
	if (!GridObj)
	{
		return Out;
	}

	const USimGrid_FeatureSettings* Features = USimGrid_FeatureSettings::Get();
	const int32 VisitCap = Features ? Features->GetSafeMaxLabelRegionCells() : 16384;

	const int32 MinX = FMath::Min(Min.X, Max.X);
	const int32 MaxX = FMath::Max(Min.X, Max.X);
	const int32 MinY = FMath::Min(Min.Y, Max.Y);
	const int32 MaxY = FMath::Max(Min.Y, Max.Y);

	auto InWindow = [&](const FSeam_CellCoord& C)
	{
		return C.X >= MinX && C.X <= MaxX && C.Y >= MinY && C.Y <= MaxY;
	};
	auto Matches = [&](const FSeam_CellCoord& C)
	{
		const FSeam_CellSnapshot Snap = ISeam_TileProviderRead::Execute_GetCellSnapshot(GridObj, C);
		return CellMatches(GridObj, Snap, MatchCategory);
	};

	int32 Visited = 0;
	TArray<FSeam_CellCoord> Stack;
	TArray<FSeam_CellCoord> Neighbours;

	for (int32 Y = MinY; Y <= MaxY; ++Y)
	{
		for (int32 X = MinX; X <= MaxX; ++X)
		{
			const FSeam_CellCoord Seed(X, Y);
			if (Out.RegionIdByCell.Contains(Seed) || !Matches(Seed))
			{
				continue;
			}

			// New region: flood fill it.
			const int32 RegionId = Out.RegionCount++;
			Out.RegionSizes.Add(0);
			Stack.Reset();
			Stack.Push(Seed);
			Out.RegionIdByCell.Add(Seed, RegionId);

			while (Stack.Num() > 0)
			{
				if (++Visited > VisitCap)
				{
					Out.bTruncated = true;
					return Out;
				}
				const FSeam_CellCoord Cur = Stack.Pop(EAllowShrinking::No);
				Out.RegionSizes[RegionId]++;

				FSimGrid_CoordMath::GetNeighbours(Cur,
					Adjacency == ESimGrid_Adjacency::Eight ? ESimGrid_Adjacency::Eight : ESimGrid_Adjacency::Four,
					Neighbours);
				for (const FSeam_CellCoord& N : Neighbours)
				{
					if (InWindow(N) && !Out.RegionIdByCell.Contains(N) && Matches(N))
					{
						Out.RegionIdByCell.Add(N, RegionId);
						Stack.Push(N);
					}
				}
			}
		}
	}
	return Out;
}

int32 USimGrid_AutoTileLib::MarchingSquaresCase(const TScriptInterface<ISeam_TileProviderRead>& Grid,
	const FSeam_CellCoord& Corner, FGameplayTag MatchCategory)
{
	UObject* GridObj = Grid ? Grid.GetObject() : nullptr;
	if (!GridObj)
	{
		return 0;
	}
	auto Matched = [&](int32 DX, int32 DY)
	{
		const FSeam_CellCoord C(Corner.X + DX, Corner.Y + DY);
		const FSeam_CellSnapshot Snap = ISeam_TileProviderRead::Execute_GetCellSnapshot(GridObj, C);
		return CellMatches(GridObj, Snap, MatchCategory);
	};
	// Bit 1 = (0,0), 2 = (+1,0), 4 = (+1,+1), 8 = (0,+1) — counter-clockwise from bottom-left.
	int32 Case = 0;
	Case |= Matched(0, 0) ? 1 : 0;
	Case |= Matched(1, 0) ? 2 : 0;
	Case |= Matched(1, 1) ? 4 : 0;
	Case |= Matched(0, 1) ? 8 : 0;
	return Case;
}
