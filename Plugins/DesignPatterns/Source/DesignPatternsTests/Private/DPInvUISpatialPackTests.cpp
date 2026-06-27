// Copyright DesignPatterns plugin. All Rights Reserved.

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Containers/BitArray.h"
#include "Strategy/InvUI_SpatialLayout.h"

// UInvUI_SpatialLayout::TryPlace / MarkAt are the pure static core of the Tetris/Diablo spatial
// bin-packer: first-fit a rectangle into a row-major TBitArray occupancy grid (index = Y*Cols + X),
// growing rows as needed and rejecting out-of-bounds placements. No world/UObject needed. These tests
// pin the placement geometry, the no-overlap invariant, and the bounds rejections that silently break.

namespace DPSpatialTestUtil
{
	// Count set cells in the occupancy bitset (the total area placed so far).
	static int32 CountSet(const TBitArray<>& Bits)
	{
		int32 N = 0;
		for (int32 i = 0; i < Bits.Num(); ++i)
		{
			if (Bits[i]) { ++N; }
		}
		return N;
	}

	static bool At(const TBitArray<>& Bits, int32 Cols, int32 X, int32 Y)
	{
		const int32 Index = Y * Cols + X;
		return Index < Bits.Num() && Bits[Index];
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDPSpatialFirstFitFlowTest,
	"DesignPatterns.InventoryUI.Spatial.FirstFitFlow",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDPSpatialFirstFitFlowTest::RunTest(const FString& /*Parameters*/)
{
	using namespace DPSpatialTestUtil;
	const int32 Cols = 4;
	const int32 MaxRows = 16;
	int32 RowCount = 0;
	TBitArray<> Occ;

	// First 2x2 lands at the top-left (0,0).
	FIntPoint C1;
	TestTrue(TEXT("place A succeeds"), UInvUI_SpatialLayout::TryPlace(FIntPoint(2, 2), Occ, Cols, RowCount, MaxRows, C1));
	TestTrue(TEXT("A at (0,0)"), C1 == FIntPoint(0, 0));

	// Second 2x2 flows to the right, (2,0).
	FIntPoint C2;
	TestTrue(TEXT("place B succeeds"), UInvUI_SpatialLayout::TryPlace(FIntPoint(2, 2), Occ, Cols, RowCount, MaxRows, C2));
	TestTrue(TEXT("B at (2,0)"), C2 == FIntPoint(2, 0));

	// Third 2x2 cannot fit on row 0 (full) -> wraps to the next free row (0,2).
	FIntPoint C3;
	TestTrue(TEXT("place C succeeds"), UInvUI_SpatialLayout::TryPlace(FIntPoint(2, 2), Occ, Cols, RowCount, MaxRows, C3));
	TestTrue(TEXT("C wraps to (0,2)"), C3 == FIntPoint(0, 2));

	// No-overlap invariant: three 2x2 items == 12 occupied cells.
	TestEqual(TEXT("total occupied area is 12"), CountSet(Occ), 12);

	// A 1x1 fills the next first-fit gap at (2,2) (row 2 still has the right half free).
	FIntPoint C4;
	TestTrue(TEXT("place 1x1 succeeds"), UInvUI_SpatialLayout::TryPlace(FIntPoint(1, 1), Occ, Cols, RowCount, MaxRows, C4));
	TestTrue(TEXT("1x1 first-fits at (2,2)"), C4 == FIntPoint(2, 2));
	TestEqual(TEXT("total occupied area is 13"), CountSet(Occ), 13);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDPSpatialTooWideTest,
	"DesignPatterns.InventoryUI.Spatial.TooWideRejected",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDPSpatialTooWideTest::RunTest(const FString& /*Parameters*/)
{
	const int32 Cols = 4;
	int32 RowCount = 0;
	TBitArray<> Occ;
	FIntPoint Out;

	// An item wider than the whole grid can never be placed.
	TestFalse(TEXT("5-wide item rejected on a 4-wide grid"),
		UInvUI_SpatialLayout::TryPlace(FIntPoint(5, 1), Occ, Cols, RowCount, /*MaxRows*/ 16, Out));
	// And it never marks anything.
	TestEqual(TEXT("nothing occupied after rejected place"), DPSpatialTestUtil::CountSet(Occ), 0);

	// Zero columns is a hard reject.
	int32 ZRows = 0;
	TBitArray<> ZOcc;
	TestFalse(TEXT("zero columns rejects"),
		UInvUI_SpatialLayout::TryPlace(FIntPoint(1, 1), ZOcc, /*Cols*/ 0, ZRows, 16, Out));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDPSpatialRowCeilingTest,
	"DesignPatterns.InventoryUI.Spatial.RowCeiling",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDPSpatialRowCeilingTest::RunTest(const FString& /*Parameters*/)
{
	const int32 Cols = 2;
	const int32 MaxRows = 2; // only rows 0..1 are usable
	int32 RowCount = 0;
	TBitArray<> Occ;

	// Fill the entire 2x2 usable area with a single 2x2 item.
	FIntPoint C1;
	TestTrue(TEXT("2x2 fills the 2-row grid"), UInvUI_SpatialLayout::TryPlace(FIntPoint(2, 2), Occ, Cols, RowCount, MaxRows, C1));
	TestTrue(TEXT("filled at (0,0)"), C1 == FIntPoint(0, 0));

	// Any further item must overflow the row ceiling and be rejected.
	FIntPoint C2;
	TestFalse(TEXT("no room left under the row ceiling"),
		UInvUI_SpatialLayout::TryPlace(FIntPoint(1, 1), Occ, Cols, RowCount, MaxRows, C2));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDPSpatialMarkAtBoundsTest,
	"DesignPatterns.InventoryUI.Spatial.MarkAtBounds",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDPSpatialMarkAtBoundsTest::RunTest(const FString& /*Parameters*/)
{
	using namespace DPSpatialTestUtil;
	const int32 Cols = 4;
	int32 RowCount = 0;
	TBitArray<> Occ;

	// A valid explicit mark at (1,1) of a 2x2 occupies exactly those 4 cells and grows to 3 rows.
	TestTrue(TEXT("mark 2x2 at (1,1) succeeds"),
		UInvUI_SpatialLayout::MarkAt(FIntPoint(1, 1), FIntPoint(2, 2), Occ, Cols, RowCount, /*MaxRows*/ 8));
	TestEqual(TEXT("row count grew to 3"), RowCount, 3);
	TestEqual(TEXT("exactly 4 cells set"), CountSet(Occ), 4);
	TestTrue(TEXT("(1,1) set"), At(Occ, Cols, 1, 1));
	TestTrue(TEXT("(2,2) set"), At(Occ, Cols, 2, 2));
	TestFalse(TEXT("(0,0) not set"), At(Occ, Cols, 0, 0));

	// Column overflow: anchoring a 2-wide item at column 3 of a 4-wide grid spills past the edge -> reject, no marks added.
	const int32 BeforeCount = CountSet(Occ);
	TestFalse(TEXT("column overflow rejected"),
		UInvUI_SpatialLayout::MarkAt(FIntPoint(3, 0), FIntPoint(2, 1), Occ, Cols, RowCount, 8));
	TestEqual(TEXT("rejected mark added nothing"), CountSet(Occ), BeforeCount);

	// Negative cell rejected.
	TestFalse(TEXT("negative cell rejected"),
		UInvUI_SpatialLayout::MarkAt(FIntPoint(-1, 0), FIntPoint(1, 1), Occ, Cols, RowCount, 8));

	return true;
}

#endif // WITH_AUTOMATION_TESTS
