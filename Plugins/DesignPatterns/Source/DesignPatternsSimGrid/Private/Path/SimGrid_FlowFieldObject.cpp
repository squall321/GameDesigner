// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Path/SimGrid_FlowFieldObject.h"
#include "Path/SimGrid_PathQuerySubsystem.h"
#include "Settings/SimGrid_FeatureSettings.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"

float USimGrid_FlowFieldObject::GetUnreachableCost()
{
	const USimGrid_FeatureSettings* Features = USimGrid_FeatureSettings::Get();
	return Features ? Features->GetUnreachableCost() : 1.e9f;
}

bool USimGrid_FlowFieldObject::BuildField(const TScriptInterface<ISeam_TileProviderRead>& Grid,
	const FSeam_CellCoord& GoalCell, const FSeam_CellCoord& WindowMin, const FSeam_CellCoord& WindowMax,
	ESimGrid_Adjacency Adjacency)
{
	Reset();

	UObject* GridObj = Grid ? Grid.GetObject() : nullptr;
	if (!GridObj)
	{
		return false;
	}

	// Reuse the path subsystem's walkability + cost rules so flow and A* agree on the world.
	USimGrid_PathQuerySubsystem* PathQuery =
		FDP_SubsystemStatics::GetWorldSubsystem<USimGrid_PathQuerySubsystem>(this);
	if (!PathQuery)
	{
		return false;
	}
	if (!PathQuery->IsCellWalkable(Grid, GoalCell))
	{
		return false;
	}

	CachedCellSize = ISeam_TileProviderRead::Execute_GetCellSize(GridObj);

	const USimGrid_FeatureSettings* Features = USimGrid_FeatureSettings::Get();
	const int32 NodeBudget = Features ? Features->GetSafeMaxPathNodes() : 8192;
	const bool bEightWay = (Adjacency == ESimGrid_Adjacency::Eight);
	const float DiagMult = Features ? Features->GetSafeDiagonalMultiplier() : 1.41421356f;

	const int32 MinX = FMath::Min(WindowMin.X, WindowMax.X);
	const int32 MaxX = FMath::Max(WindowMin.X, WindowMax.X);
	const int32 MinY = FMath::Min(WindowMin.Y, WindowMax.Y);
	const int32 MaxY = FMath::Max(WindowMin.Y, WindowMax.Y);

	auto InWindow = [&](const FSeam_CellCoord& C)
	{
		return C.X >= MinX && C.X <= MaxX && C.Y >= MinY && C.Y <= MaxY;
	};

	// Dijkstra from the goal outward: cost is the cumulative ENTRY cost of stepping FROM a neighbour INTO
	// the cell currently being expanded, so a cell's integration cost equals the best path cost to goal.
	struct FFrontier
	{
		FSeam_CellCoord Cell;
		float Cost = 0.f;
	};
	auto Pred = [](const FFrontier& A, const FFrontier& B) { return A.Cost < B.Cost; };

	TArray<FFrontier> Open;
	Open.HeapPush(FFrontier{ GoalCell, 0.f }, Pred);
	CostByCell.Add(GoalCell, 0.f);

	int32 Popped = 0;
	TArray<FSeam_CellCoord> Neighbours;

	while (Open.Num() > 0)
	{
		if (++Popped > NodeBudget)
		{
			UE_LOG(LogDP, Verbose, TEXT("SimGrid flow field hit node budget (%d); field truncated."), NodeBudget);
			break;
		}

		FFrontier Current;
		Open.HeapPop(Current, Pred);

		if (const float* Recorded = CostByCell.Find(Current.Cell))
		{
			if (Current.Cost > *Recorded + KINDA_SMALL_NUMBER)
			{
				continue; // stale heap entry
			}
		}

		FSimGrid_CoordMath::GetNeighbours(Current.Cell,
			bEightWay ? ESimGrid_Adjacency::Eight : ESimGrid_Adjacency::Four, Neighbours);

		for (const FSeam_CellCoord& Next : Neighbours)
		{
			if (!InWindow(Next) || !PathQuery->IsCellWalkable(Grid, Next))
			{
				continue;
			}
			const bool bDiagonal = (Next.X != Current.Cell.X) && (Next.Y != Current.Cell.Y);
			if (bDiagonal)
			{
				const bool bAllowCornerCut = (Features && Features->bAllowDiagonalCornerCut);
				if (!bAllowCornerCut)
				{
					// No-corner-cut: a diagonal step is only legal when both orthogonal cells between
					// Current and Next are walkable, matching the A* rule so flow and A* agree.
					if (!PathQuery->IsCellWalkable(Grid, FSeam_CellCoord(Next.X, Current.Cell.Y))
						|| !PathQuery->IsCellWalkable(Grid, FSeam_CellCoord(Current.Cell.X, Next.Y)))
					{
						continue;
					}
				}
			}

			// Cost to ENTER Current from Next is Current's entry cost (we expand goal-ward, so the agent
			// at Next will step into Current; using Current's entry cost keeps it symmetric with A*).
			float StepCost = PathQuery->GetCellEntryCost(Grid, Current.Cell, 0.f);
			if (bDiagonal)
			{
				StepCost *= DiagMult;
			}
			const float Tentative = Current.Cost + StepCost;

			const float* Existing = CostByCell.Find(Next);
			if (!Existing || Tentative < *Existing - KINDA_SMALL_NUMBER)
			{
				CostByCell.Add(Next, Tentative);
				NextByCell.Add(Next, Current.Cell); // step toward goal == toward lower cost
				Open.HeapPush(FFrontier{ Next, Tentative }, Pred);
			}
		}
	}

	Goal = GoalCell;
	bBuilt = true;
	return true;
}

void USimGrid_FlowFieldObject::Reset()
{
	CostByCell.Reset();
	NextByCell.Reset();
	bBuilt = false;
}

//~ ISimGrid_FlowField --------------------------------------------------------------------------

bool USimGrid_FlowFieldObject::HasFlowAt_Implementation(const FSeam_CellCoord& Cell) const
{
	return bBuilt && CostByCell.Contains(Cell);
}

FVector USimGrid_FlowFieldObject::GetFlowDirection_Implementation(const FSeam_CellCoord& Cell) const
{
	if (!bBuilt)
	{
		return FVector::ZeroVector;
	}
	if (Cell == Goal)
	{
		return FVector::ZeroVector; // already at the goal
	}
	if (const FSeam_CellCoord* Next = NextByCell.Find(Cell))
	{
		// World-XY direction from this cell toward its best next cell.
		const FVector Dir(
			static_cast<double>(Next->X - Cell.X),
			static_cast<double>(Next->Y - Cell.Y),
			0.0);
		return Dir.GetSafeNormal();
	}
	return FVector::ZeroVector;
}

float USimGrid_FlowFieldObject::GetIntegrationCost_Implementation(const FSeam_CellCoord& Cell) const
{
	if (bBuilt)
	{
		if (const float* Cost = CostByCell.Find(Cell))
		{
			return *Cost;
		}
	}
	return GetUnreachableCost();
}

FSeam_CellCoord USimGrid_FlowFieldObject::GetGoalCell_Implementation() const
{
	return Goal;
}
