// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Query/SimGrid_QuerySubsystem.h"
#include "SimGrid_DeveloperSettings.h"
#include "Core/DPLog.h"
#include "Grid/Seam_TileProviderRead.h"
#include "Containers/Queue.h"

void USimGrid_QuerySubsystem::GetCaps(int32& OutMaxRadius, int32& OutMaxRegion, int32& OutMaxFlood, int32& OutMaxLine)
{
	const USimGrid_DeveloperSettings* Settings = USimGrid_DeveloperSettings::Get();
	if (Settings)
	{
		OutMaxRadius = FMath::Max(1, Settings->MaxQueryRadiusCells);
		OutMaxRegion = FMath::Max(1, Settings->MaxRegionCells);
		OutMaxFlood  = FMath::Max(1, Settings->MaxFloodFillCells);
		OutMaxLine   = FMath::Max(1, Settings->MaxLineCells);
	}
	else
	{
		// Settings CDO should always exist; fall back to the declared defaults defensively.
		OutMaxRadius = 64;
		OutMaxRegion = 4096;
		OutMaxFlood  = 8192;
		OutMaxLine   = 1024;
	}
}

bool USimGrid_QuerySubsystem::SnapshotMatches(const FSeam_CellSnapshot& Snapshot, const FGameplayTag& MatchTileType)
{
	if (!Snapshot.IsKnown())
	{
		// Unknown cells never match and are treated as impassable by callers.
		return false;
	}
	if (!MatchTileType.IsValid())
	{
		// An invalid match tag means "match known-empty cells".
		return Snapshot.KnownState == ESeam_KnownState::Empty;
	}
	return Snapshot.IsSet() && Snapshot.TileTypeTag.IsValid() && Snapshot.TileTypeTag.MatchesTag(MatchTileType);
}

TArray<FSeam_CellCoord> USimGrid_QuerySubsystem::GetNeighbors(const TScriptInterface<ISeam_TileProviderRead>& Grid,
	const FSeam_CellCoord& Cell, ESimGrid_Adjacency Adjacency) const
{
	TArray<FSeam_CellCoord> Result;
	UObject* GridObj = Grid.GetObject();
	if (!GridObj)
	{
		return Result;
	}

	static const FSeam_CellCoord Ortho[4] =
	{
		FSeam_CellCoord(1, 0), FSeam_CellCoord(-1, 0), FSeam_CellCoord(0, 1), FSeam_CellCoord(0, -1)
	};
	static const FSeam_CellCoord Diag[4] =
	{
		FSeam_CellCoord(1, 1), FSeam_CellCoord(1, -1), FSeam_CellCoord(-1, 1), FSeam_CellCoord(-1, -1)
	};

	Result.Reserve(8);
	for (const FSeam_CellCoord& D : Ortho)
	{
		const FSeam_CellCoord N = Cell + D;
		if (ISeam_TileProviderRead::Execute_IsValidCell(GridObj, N))
		{
			Result.Add(N);
		}
	}
	if (Adjacency == ESimGrid_Adjacency::EightWay)
	{
		for (const FSeam_CellCoord& D : Diag)
		{
			const FSeam_CellCoord N = Cell + D;
			if (ISeam_TileProviderRead::Execute_IsValidCell(GridObj, N))
			{
				Result.Add(N);
			}
		}
	}
	return Result;
}

TArray<FSeam_CellCoord> USimGrid_QuerySubsystem::GetRegion(const TScriptInterface<ISeam_TileProviderRead>& Grid,
	const FSeam_CellCoord& Center, int32 RadiusCells, ESimGrid_RegionShape Shape) const
{
	TArray<FSeam_CellCoord> Result;
	UObject* GridObj = Grid.GetObject();
	if (!GridObj)
	{
		return Result;
	}

	int32 MaxRadius, MaxRegion, MaxFlood, MaxLine;
	GetCaps(MaxRadius, MaxRegion, MaxFlood, MaxLine);

	const int32 R = FMath::Clamp(RadiusCells, 0, MaxRadius);

	// Emit nearest-first by Chebyshev ring so a region truncated at MaxRegion keeps the closest cells.
	for (int32 Ring = 0; Ring <= R && Result.Num() < MaxRegion; ++Ring)
	{
		for (int32 dy = -Ring; dy <= Ring && Result.Num() < MaxRegion; ++dy)
		{
			for (int32 dx = -Ring; dx <= Ring && Result.Num() < MaxRegion; ++dx)
			{
				// Only the cells on the current Chebyshev ring (max(|dx|,|dy|) == Ring).
				if (FMath::Max(FMath::Abs(dx), FMath::Abs(dy)) != Ring)
				{
					continue;
				}

				// Shape test.
				bool bInShape = true;
				switch (Shape)
				{
				case ESimGrid_RegionShape::Disc:
					bInShape = (dx * dx + dy * dy) <= (R * R);
					break;
				case ESimGrid_RegionShape::Diamond:
					bInShape = (FMath::Abs(dx) + FMath::Abs(dy)) <= R;
					break;
				case ESimGrid_RegionShape::Square:
				default:
					bInShape = true;
					break;
				}
				if (!bInShape)
				{
					continue;
				}

				const FSeam_CellCoord Cell = Center + FSeam_CellCoord(dx, dy);
				if (ISeam_TileProviderRead::Execute_IsValidCell(GridObj, Cell))
				{
					Result.Add(Cell);
				}
			}
		}
	}

	if (Result.Num() >= MaxRegion)
	{
		UE_LOG(LogDP, Verbose, TEXT("[SimGrid_Query] GetRegion truncated at MaxRegionCells=%d (radius %d)."), MaxRegion, R);
	}
	return Result;
}

TArray<FSeam_CellCoord> USimGrid_QuerySubsystem::FloodFill(const TScriptInterface<ISeam_TileProviderRead>& Grid,
	const FSeam_CellCoord& Start, FGameplayTag MatchTileType, bool& bOutTruncated) const
{
	bOutTruncated = false;
	TArray<FSeam_CellCoord> Result;
	UObject* GridObj = Grid.GetObject();
	if (!GridObj)
	{
		return Result;
	}

	int32 MaxRadius, MaxRegion, MaxFlood, MaxLine;
	GetCaps(MaxRadius, MaxRegion, MaxFlood, MaxLine);

	// The seed must itself be valid and matching, else the fill is empty.
	if (!ISeam_TileProviderRead::Execute_IsValidCell(GridObj, Start))
	{
		return Result;
	}
	{
		const FSeam_CellSnapshot StartSnap = ISeam_TileProviderRead::Execute_GetCellSnapshot(GridObj, Start);
		if (!SnapshotMatches(StartSnap, MatchTileType))
		{
			return Result;
		}
	}

	static const FSeam_CellCoord Ortho[4] =
	{
		FSeam_CellCoord(1, 0), FSeam_CellCoord(-1, 0), FSeam_CellCoord(0, 1), FSeam_CellCoord(0, -1)
	};

	TSet<FSeam_CellCoord> Visited;
	Visited.Reserve(FMath::Min(MaxFlood, 1024));
	TQueue<FSeam_CellCoord> Frontier;

	Visited.Add(Start);
	Frontier.Enqueue(Start);

	FSeam_CellCoord Current;
	while (Frontier.Dequeue(Current))
	{
		if (Result.Num() >= MaxFlood)
		{
			bOutTruncated = true;
			break;
		}
		Result.Add(Current);

		for (const FSeam_CellCoord& D : Ortho)
		{
			const FSeam_CellCoord N = Current + D;
			if (Visited.Contains(N))
			{
				continue;
			}
			if (Visited.Num() >= MaxFlood)
			{
				// Bound the visited set too so we don't enqueue unboundedly.
				bOutTruncated = true;
				break;
			}
			if (!ISeam_TileProviderRead::Execute_IsValidCell(GridObj, N))
			{
				continue;
			}
			const FSeam_CellSnapshot Snap = ISeam_TileProviderRead::Execute_GetCellSnapshot(GridObj, N);
			if (SnapshotMatches(Snap, MatchTileType))
			{
				Visited.Add(N);
				Frontier.Enqueue(N);
			}
		}
	}

	if (bOutTruncated)
	{
		UE_LOG(LogDP, Verbose, TEXT("[SimGrid_Query] FloodFill truncated at MaxFloodFillCells=%d."), MaxFlood);
	}
	return Result;
}

TArray<FSeam_CellCoord> USimGrid_QuerySubsystem::GetLine(const TScriptInterface<ISeam_TileProviderRead>& Grid,
	const FSeam_CellCoord& Start, const FSeam_CellCoord& End) const
{
	TArray<FSeam_CellCoord> Result;
	UObject* GridObj = Grid.GetObject();
	if (!GridObj)
	{
		return Result;
	}

	int32 MaxRadius, MaxRegion, MaxFlood, MaxLine;
	GetCaps(MaxRadius, MaxRegion, MaxFlood, MaxLine);

	// Integer Bresenham line from Start to End, inclusive.
	int32 X0 = Start.X, Y0 = Start.Y;
	const int32 X1 = End.X, Y1 = End.Y;

	const int32 DX = FMath::Abs(X1 - X0);
	const int32 DY = -FMath::Abs(Y1 - Y0);
	const int32 SX = (X0 < X1) ? 1 : -1;
	const int32 SY = (Y0 < Y1) ? 1 : -1;
	int32 Err = DX + DY;

	while (Result.Num() < MaxLine)
	{
		const FSeam_CellCoord Cell(X0, Y0);
		if (ISeam_TileProviderRead::Execute_IsValidCell(GridObj, Cell))
		{
			Result.Add(Cell);
		}

		if (X0 == X1 && Y0 == Y1)
		{
			break;
		}
		const int32 E2 = 2 * Err;
		if (E2 >= DY)
		{
			Err += DY;
			X0 += SX;
		}
		if (E2 <= DX)
		{
			Err += DX;
			Y0 += SY;
		}
	}

	if (Result.Num() >= MaxLine)
	{
		UE_LOG(LogDP, Verbose, TEXT("[SimGrid_Query] GetLine truncated at MaxLineCells=%d."), MaxLine);
	}
	return Result;
}

FString USimGrid_QuerySubsystem::GetDPDebugString_Implementation() const
{
	int32 MaxRadius, MaxRegion, MaxFlood, MaxLine;
	GetCaps(MaxRadius, MaxRegion, MaxFlood, MaxLine);
	return FString::Printf(TEXT("SimGrid_Query: caps radius=%d region=%d flood=%d line=%d (authority=%s)"),
		MaxRadius, MaxRegion, MaxFlood, MaxLine, HasWorldAuthority() ? TEXT("yes") : TEXT("no"));
}
