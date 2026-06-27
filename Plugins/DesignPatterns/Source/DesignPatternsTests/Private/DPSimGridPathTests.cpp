// Copyright DesignPatterns plugin. All Rights Reserved.

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

#include "Path/SimGrid_PathQuerySubsystem.h"
#include "Path/SimGrid_PathTypes.h"
#include "Grid/Seam_TileProviderRead.h"
#include "Grid/Seam_GridCoord.h"
#include "World/SimGrid_CoordTypes.h"

#include "DPSimGridPathTests.generated.h"

// A walkable-floor mock grid for testing USimGrid_PathQuerySubsystem::FindPathOnGrid with no world.
// IsCellWalkable treats a KNOWN-EMPTY cell as walkable ground and a Set/Unknown cell as a wall (a Set
// cell needs a tile-definition lookup via the data registry, which is absent in a transient instance,
// so it is defensively blocked). So: cells in Floor report Empty (walkable); every other cell reports
// Unknown (wall). With no data registry, entry cost is uniform, so path COST tracks path length.
UCLASS(Transient, NotBlueprintable)
class UDPTest_PathGrid : public UObject, public ISeam_TileProviderRead
{
	GENERATED_BODY()

public:
	TSet<FSeam_CellCoord> Floor;

	void AddFloor(int32 X, int32 Y) { Floor.Add(FSeam_CellCoord(X, Y)); }
	void AddRow(int32 Y, int32 X0, int32 X1) { for (int32 X = X0; X <= X1; ++X) { AddFloor(X, Y); } }

	virtual FSeam_CellSnapshot GetCellSnapshot_Implementation(const FSeam_CellCoord& Cell) const override
	{
		FSeam_CellSnapshot Snap;
		Snap.KnownState = Floor.Contains(Cell) ? ESeam_KnownState::Empty : ESeam_KnownState::Unknown;
		return Snap;
	}
	virtual bool IsValidCell_Implementation(const FSeam_CellCoord& /*Cell*/) const override { return true; }
	virtual FSeam_CellCoord WorldToCell_Implementation(const FVector& /*W*/) const override { return FSeam_CellCoord(); }
	virtual FVector CellToWorld_Implementation(const FSeam_CellCoord& /*C*/, bool /*bCenter*/) const override { return FVector::ZeroVector; }
	virtual float GetCellSize_Implementation() const override { return 100.f; }
};

namespace DPPathTestUtil
{
	static USimGrid_PathQuerySubsystem* NewQuery()
	{
		// A bare subsystem instance: FindPathOnGrid takes the grid explicitly and every internal subsystem
		// lookup (data registry, height provider) null-guards, so no Initialize / world is needed.
		return NewObject<USimGrid_PathQuerySubsystem>(GetTransientPackage());
	}
	static TScriptInterface<ISeam_TileProviderRead> AsSeam(UDPTest_PathGrid* Grid)
	{
		TScriptInterface<ISeam_TileProviderRead> S;
		S.SetObject(Grid);
		S.SetInterface(Cast<ISeam_TileProviderRead>(Grid));
		return S;
	}
	// True if consecutive cells in the path are grid-adjacent (4- or 8-step) and endpoints match.
	static bool IsContiguous(const FSimGrid_PathResult& R, const FSeam_CellCoord& Start, const FSeam_CellCoord& Goal)
	{
		if (R.Cells.Num() == 0 || !(R.Cells[0] == Start) || !(R.Cells.Last() == Goal))
		{
			return false;
		}
		for (int32 i = 1; i < R.Cells.Num(); ++i)
		{
			const int32 DX = FMath::Abs(R.Cells[i].X - R.Cells[i - 1].X);
			const int32 DY = FMath::Abs(R.Cells[i].Y - R.Cells[i - 1].Y);
			if (DX > 1 || DY > 1 || (DX == 0 && DY == 0))
			{
				return false; // not a single grid step
			}
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDPPathStraightTest,
	"DesignPatterns.SimGrid.Path.StraightLine",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDPPathStraightTest::RunTest(const FString& /*Parameters*/)
{
	using namespace DPPathTestUtil;
	UDPTest_PathGrid* Grid = NewObject<UDPTest_PathGrid>(GetTransientPackage());
	Grid->AddRow(/*Y*/ 0, /*X0*/ 0, /*X1*/ 5); // a 6-cell floor strip on row 0

	USimGrid_PathQuerySubsystem* Q = NewQuery();
	FSimGrid_PathRequest Req(FSeam_CellCoord(0, 0), FSeam_CellCoord(5, 0));
	Req.Adjacency = ESimGrid_Adjacency::Four;
	const FSimGrid_PathResult R = Q->FindPathOnGrid(AsSeam(Grid), Req);

	TestEqual(TEXT("straight path succeeds"), R.Result, ESimGrid_PathResult::Success);
	TestTrue(TEXT("result is valid"), R.IsValid());
	TestEqual(TEXT("5 steps across 6 cells"), R.NumSteps(), 5);
	TestEqual(TEXT("6 cells inclusive"), R.Cells.Num(), 6);
	TestTrue(TEXT("path is contiguous start->goal"), IsContiguous(R, Req.Start, Req.Goal));
	TestTrue(TEXT("positive total cost"), R.TotalCost > 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDPPathStartEqualsGoalTest,
	"DesignPatterns.SimGrid.Path.StartEqualsGoal",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDPPathStartEqualsGoalTest::RunTest(const FString& /*Parameters*/)
{
	using namespace DPPathTestUtil;
	UDPTest_PathGrid* Grid = NewObject<UDPTest_PathGrid>(GetTransientPackage());
	Grid->AddFloor(2, 2);
	USimGrid_PathQuerySubsystem* Q = NewQuery();
	const FSimGrid_PathResult R = Q->FindPathOnGrid(AsSeam(Grid), FSimGrid_PathRequest(FSeam_CellCoord(2, 2), FSeam_CellCoord(2, 2)));

	TestEqual(TEXT("start==goal succeeds"), R.Result, ESimGrid_PathResult::Success);
	TestEqual(TEXT("single-cell path"), R.Cells.Num(), 1);
	TestEqual(TEXT("zero steps"), R.NumSteps(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDPPathBlockedEndpointTest,
	"DesignPatterns.SimGrid.Path.BlockedEndpoint",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDPPathBlockedEndpointTest::RunTest(const FString& /*Parameters*/)
{
	using namespace DPPathTestUtil;
	UDPTest_PathGrid* Grid = NewObject<UDPTest_PathGrid>(GetTransientPackage());
	Grid->AddFloor(0, 0); // start is floor; goal (3,0) is left as Unknown (wall)

	USimGrid_PathQuerySubsystem* Q = NewQuery();
	const FSimGrid_PathResult R = Q->FindPathOnGrid(AsSeam(Grid), FSimGrid_PathRequest(FSeam_CellCoord(0, 0), FSeam_CellCoord(3, 0)));

	TestEqual(TEXT("unwalkable goal -> BlockedEndpoint"), R.Result, ESimGrid_PathResult::BlockedEndpoint);
	TestEqual(TEXT("no cells on failure"), R.Cells.Num(), 0);
	TestFalse(TEXT("result not valid"), R.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDPPathNoPathTest,
	"DesignPatterns.SimGrid.Path.NoPath",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDPPathNoPathTest::RunTest(const FString& /*Parameters*/)
{
	using namespace DPPathTestUtil;
	// Two walkable cells with NO connecting floor between them: start (0,0) and goal (2,0), but (1,0) is a wall.
	UDPTest_PathGrid* Grid = NewObject<UDPTest_PathGrid>(GetTransientPackage());
	Grid->AddFloor(0, 0);
	Grid->AddFloor(2, 0);
	USimGrid_PathQuerySubsystem* Q = NewQuery();
	FSimGrid_PathRequest Req(FSeam_CellCoord(0, 0), FSeam_CellCoord(2, 0));
	Req.Adjacency = ESimGrid_Adjacency::Eight; // even diagonally there is no route
	const FSimGrid_PathResult R = Q->FindPathOnGrid(AsSeam(Grid), Req);

	TestEqual(TEXT("walled-off goal -> NoPath"), R.Result, ESimGrid_PathResult::NoPath);
	TestEqual(TEXT("no cells on NoPath"), R.Cells.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDPPathAroundWallTest,
	"DesignPatterns.SimGrid.Path.AroundWall",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDPPathAroundWallTest::RunTest(const FString& /*Parameters*/)
{
	using namespace DPPathTestUtil;
	// An L-shaped corridor (4-connected): right along row 0 from (0,0) to (2,0), then up to (2,2).
	// The direct diagonal is blocked because only the L cells are floor.
	UDPTest_PathGrid* Grid = NewObject<UDPTest_PathGrid>(GetTransientPackage());
	Grid->AddRow(0, 0, 2);   // (0,0)(1,0)(2,0)
	Grid->AddFloor(2, 1);
	Grid->AddFloor(2, 2);
	USimGrid_PathQuerySubsystem* Q = NewQuery();
	FSimGrid_PathRequest Req(FSeam_CellCoord(0, 0), FSeam_CellCoord(2, 2));
	Req.Adjacency = ESimGrid_Adjacency::Four;
	const FSimGrid_PathResult R = Q->FindPathOnGrid(AsSeam(Grid), Req);

	TestEqual(TEXT("L-corridor path succeeds"), R.Result, ESimGrid_PathResult::Success);
	// The only route is the 5-cell L: (0,0)(1,0)(2,0)(2,1)(2,2) -> 4 steps.
	TestEqual(TEXT("L path is 4 steps"), R.NumSteps(), 4);
	TestEqual(TEXT("L path visits all 5 corridor cells"), R.Cells.Num(), 5);
	TestTrue(TEXT("L path contiguous"), IsContiguous(R, Req.Start, Req.Goal));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
