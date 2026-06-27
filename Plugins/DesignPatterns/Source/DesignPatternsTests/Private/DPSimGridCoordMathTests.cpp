// Copyright DesignPatterns plugin. All Rights Reserved.

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "World/SimGrid_CoordTypes.h"
#include "Grid/Seam_GridCoord.h"

// FSimGrid_CoordMath is pure, allocation-free grid coordinate math. Its whole reason to exist is that
// C++ integer division/modulo truncate toward zero, which is WRONG for negative cells — so these helpers
// floor-divide and positive-modulo instead. These tests pin that behaviour (the negative-coordinate
// edge cases are exactly what silently regresses) plus rotation composition, distance metrics, and the
// neighbour ordering that callers rely on (first 4 are the cardinals for both adjacency models).

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDPCoordMathFloorDivPosModTest,
	"DesignPatterns.SimGrid.CoordMath.FloorDivPosMod",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDPCoordMathFloorDivPosModTest::RunTest(const FString& /*Parameters*/)
{
	// FloorDiv rounds toward negative infinity.
	TestEqual(TEXT("FloorDiv(8,8)"), FSimGrid_CoordMath::FloorDiv(8, 8), 1);
	TestEqual(TEXT("FloorDiv(7,8)"), FSimGrid_CoordMath::FloorDiv(7, 8), 0);
	TestEqual(TEXT("FloorDiv(0,8)"), FSimGrid_CoordMath::FloorDiv(0, 8), 0);
	TestEqual(TEXT("FloorDiv(-1,8) == -1 (not 0)"), FSimGrid_CoordMath::FloorDiv(-1, 8), -1);
	TestEqual(TEXT("FloorDiv(-8,8) == -1"), FSimGrid_CoordMath::FloorDiv(-8, 8), -1);
	TestEqual(TEXT("FloorDiv(-9,8) == -2"), FSimGrid_CoordMath::FloorDiv(-9, 8), -2);

	// PosMod wraps into [0,B) even for negative A.
	TestEqual(TEXT("PosMod(0,8)"), FSimGrid_CoordMath::PosMod(0, 8), 0);
	TestEqual(TEXT("PosMod(9,8)"), FSimGrid_CoordMath::PosMod(9, 8), 1);
	TestEqual(TEXT("PosMod(-1,8) == 7 (not -1)"), FSimGrid_CoordMath::PosMod(-1, 8), 7);
	TestEqual(TEXT("PosMod(-8,8) == 0"), FSimGrid_CoordMath::PosMod(-8, 8), 0);
	TestEqual(TEXT("PosMod(-9,8) == 7"), FSimGrid_CoordMath::PosMod(-9, 8), 7);

	// Invariant: A == FloorDiv(A,B)*B + PosMod(A,B) for representative A (including negatives).
	for (int32 A : { -17, -8, -1, 0, 1, 8, 17 })
	{
		const int32 B = 8;
		const int32 Recomposed = FSimGrid_CoordMath::FloorDiv(A, B) * B + FSimGrid_CoordMath::PosMod(A, B);
		TestEqual(FString::Printf(TEXT("div/mod recompose A=%d"), A), Recomposed, A);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDPCoordMathCellChunkTest,
	"DesignPatterns.SimGrid.CoordMath.CellChunkMapping",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDPCoordMathCellChunkTest::RunTest(const FString& /*Parameters*/)
{
	const FIntPoint Chunk(8, 8);

	// A cell in the origin chunk and one in the chunk to the lower-left (negative) map correctly.
	const FSimGrid_ChunkCoord C0 = FSimGrid_CoordMath::CellToChunk(FSeam_CellCoord(3, 5), Chunk);
	TestTrue(TEXT("cell (3,5) -> chunk (0,0)"), C0.X == 0 && C0.Y == 0);

	const FSimGrid_ChunkCoord CNeg = FSimGrid_CoordMath::CellToChunk(FSeam_CellCoord(-1, -1), Chunk);
	TestTrue(TEXT("cell (-1,-1) -> chunk (-1,-1)"), CNeg.X == -1 && CNeg.Y == -1);

	// Local offset within chunk is in [0,size) even for negative cells.
	const FSeam_CellCoord L = FSimGrid_CoordMath::CellToLocal(FSeam_CellCoord(-1, -1), Chunk);
	TestTrue(TEXT("local of (-1,-1) is (7,7)"), L == FSeam_CellCoord(7, 7));

	// ChunkOriginCell is the inverse anchor: origin of chunk (-1,-1) is cell (-8,-8), and (-8,-8) is local (0,0).
	const FSeam_CellCoord Origin = FSimGrid_CoordMath::ChunkOriginCell(FSimGrid_ChunkCoord(-1, -1), Chunk);
	TestTrue(TEXT("origin of chunk (-1,-1) is cell (-8,-8)"), Origin == FSeam_CellCoord(-8, -8));
	const FSeam_CellCoord OriginLocal = FSimGrid_CoordMath::CellToLocal(Origin, Chunk);
	TestTrue(TEXT("chunk origin cell has local (0,0)"), OriginLocal == FSeam_CellCoord(0, 0));

	// A zero/degenerate chunk size is clamped to 1 (no divide-by-zero, every cell is its own chunk).
	const FSimGrid_ChunkCoord Degenerate = FSimGrid_CoordMath::CellToChunk(FSeam_CellCoord(5, -3), FIntPoint(0, 0));
	TestTrue(TEXT("degenerate chunk size clamps to 1"), Degenerate.X == 5 && Degenerate.Y == -3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDPCoordMathRotationTest,
	"DesignPatterns.SimGrid.CoordMath.Rotation",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDPCoordMathRotationTest::RunTest(const FString& /*Parameters*/)
{
	const FSeam_CellCoord P(2, 1);

	// R0 is identity; the four 90-degree steps form a cycle back to the original.
	TestTrue(TEXT("R0 identity"), FSimGrid_CoordMath::RotateOffset(P, ESimGrid_Rotation::R0) == P);

	const FSeam_CellCoord R90  = FSimGrid_CoordMath::RotateOffset(P, ESimGrid_Rotation::R90);
	const FSeam_CellCoord R180 = FSimGrid_CoordMath::RotateOffset(P, ESimGrid_Rotation::R180);
	const FSeam_CellCoord R270 = FSimGrid_CoordMath::RotateOffset(P, ESimGrid_Rotation::R270);
	TestTrue(TEXT("R90 of (2,1) is (-1,2)"), R90 == FSeam_CellCoord(-1, 2));
	TestTrue(TEXT("R180 of (2,1) is (-2,-1)"), R180 == FSeam_CellCoord(-2, -1));
	TestTrue(TEXT("R270 of (2,1) is (1,-2)"), R270 == FSeam_CellCoord(1, -2));

	// Composing R90 four times returns to the original.
	FSeam_CellCoord Q = P;
	for (int32 i = 0; i < 4; ++i)
	{
		Q = FSimGrid_CoordMath::RotateOffset(Q, ESimGrid_Rotation::R90);
	}
	TestTrue(TEXT("four R90 rotations are identity"), Q == P);

	// Yaw mapping.
	TestEqual(TEXT("R0 yaw"), FSimGrid_CoordMath::RotationToYaw(ESimGrid_Rotation::R0), 0.f);
	TestEqual(TEXT("R90 yaw"), FSimGrid_CoordMath::RotationToYaw(ESimGrid_Rotation::R90), 90.f);
	TestEqual(TEXT("R270 yaw"), FSimGrid_CoordMath::RotationToYaw(ESimGrid_Rotation::R270), 270.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDPCoordMathDistanceAndNeighboursTest,
	"DesignPatterns.SimGrid.CoordMath.DistanceAndNeighbours",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDPCoordMathDistanceAndNeighboursTest::RunTest(const FString& /*Parameters*/)
{
	const FSeam_CellCoord A(0, 0);
	const FSeam_CellCoord B(3, -4);

	TestEqual(TEXT("Chebyshev (king) distance"), FSimGrid_CoordMath::ChebyshevDistance(A, B), 4);
	TestEqual(TEXT("Manhattan distance"), FSimGrid_CoordMath::ManhattanDistance(A, B), 7);
	TestEqual(TEXT("self distance is 0"), FSimGrid_CoordMath::ChebyshevDistance(A, A), 0);

	// Four-connected: exactly 4 neighbours, the cardinals.
	TArray<FSeam_CellCoord> N4;
	FSimGrid_CoordMath::GetNeighbours(FSeam_CellCoord(0, 0), ESimGrid_Adjacency::Four, N4);
	TestEqual(TEXT("4-connected yields 4 neighbours"), N4.Num(), 4);
	TestTrue(TEXT("contains +X"), N4.Contains(FSeam_CellCoord(1, 0)));
	TestTrue(TEXT("contains -X"), N4.Contains(FSeam_CellCoord(-1, 0)));
	TestTrue(TEXT("contains +Y"), N4.Contains(FSeam_CellCoord(0, 1)));
	TestTrue(TEXT("contains -Y"), N4.Contains(FSeam_CellCoord(0, -1)));
	TestFalse(TEXT("4-connected excludes diagonal"), N4.Contains(FSeam_CellCoord(1, 1)));

	// Eight-connected: 8 neighbours, and the FIRST 4 are still the cardinals (callers depend on this).
	TArray<FSeam_CellCoord> N8;
	FSimGrid_CoordMath::GetNeighbours(FSeam_CellCoord(0, 0), ESimGrid_Adjacency::Eight, N8);
	TestEqual(TEXT("8-connected yields 8 neighbours"), N8.Num(), 8);
	TestTrue(TEXT("first 4 of 8-connected are the cardinals"),
		N8[0] == FSeam_CellCoord(1, 0) && N8[1] == FSeam_CellCoord(-1, 0) &&
		N8[2] == FSeam_CellCoord(0, 1) && N8[3] == FSeam_CellCoord(0, -1));
	TestTrue(TEXT("8-connected includes a diagonal"), N8.Contains(FSeam_CellCoord(1, 1)));

	// No duplicate neighbours.
	TSet<int64> Seen;
	for (const FSeam_CellCoord& C : N8)
	{
		Seen.Add((static_cast<int64>(C.X) << 32) ^ static_cast<uint32>(C.Y));
	}
	TestEqual(TEXT("8 neighbours are distinct"), Seen.Num(), 8);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
