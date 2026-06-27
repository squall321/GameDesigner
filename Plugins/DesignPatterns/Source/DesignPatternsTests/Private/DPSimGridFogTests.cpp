// Copyright DesignPatterns plugin. All Rights Reserved.

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Fog/SimGrid_FogCarrier.h"
#include "Fog/SimGrid_FogTypes.h"
#include "Grid/Seam_GridCoord.h"

// ASimGrid_FogCarrier's run-array helpers (RunArrayContains / EnsureCellInRunArray /
// RemoveCellFromRunArray) are static and operate purely on an FSimGrid_FogRunArray — no actor state,
// no world. They are the RLE merge/query core of fog-of-war, replicated via a fast array, so a
// boundary bug here would desync clients. The Carrier* param is unused, so we pass nullptr and test
// the array logic directly on a stack array. (MarkItemDirty/MarkArrayDirty are safe on a stack array.)

namespace DPFogTestUtil
{
	// Append a (possibly multi-cell) run directly, bypassing EnsureCellInRunArray, so we can verify the
	// query helpers handle multi-cell spans (which Ensure never writes but the queries still match).
	static void AddRun(FSimGrid_FogRunArray& Arr, int32 RowY, int32 StartX, int32 EndX, bool bVisible)
	{
		FSimGrid_FogRun R;
		R.RowY = RowY; R.StartX = StartX; R.EndX = EndX; R.bCurrentlyVisible = bVisible;
		Arr.Entries.Add(R);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDPFogRunContainsTest,
	"DesignPatterns.SimGrid.Fog.RunArrayContains",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDPFogRunContainsTest::RunTest(const FString& /*Parameters*/)
{
	using namespace DPFogTestUtil;
	FSimGrid_FogRunArray Arr;
	// A multi-cell run on row 3 covering X in [2,5], and a single-cell run on row 0 at X=0.
	AddRun(Arr, /*RowY*/ 3, /*StartX*/ 2, /*EndX*/ 5, /*bVisible*/ true);
	AddRun(Arr, /*RowY*/ 0, /*StartX*/ 0, /*EndX*/ 0, /*bVisible*/ false);

	// Inside the span (inclusive on both ends) matches; just outside does not.
	TestTrue(TEXT("(2,3) start of span covered"), ASimGrid_FogCarrier::RunArrayContains(Arr, FSeam_CellCoord(2, 3)));
	TestTrue(TEXT("(4,3) middle of span covered"), ASimGrid_FogCarrier::RunArrayContains(Arr, FSeam_CellCoord(4, 3)));
	TestTrue(TEXT("(5,3) end of span covered (inclusive)"), ASimGrid_FogCarrier::RunArrayContains(Arr, FSeam_CellCoord(5, 3)));
	TestFalse(TEXT("(1,3) just before span not covered"), ASimGrid_FogCarrier::RunArrayContains(Arr, FSeam_CellCoord(1, 3)));
	TestFalse(TEXT("(6,3) just after span not covered"), ASimGrid_FogCarrier::RunArrayContains(Arr, FSeam_CellCoord(6, 3)));

	// Wrong row never matches even at a covered X.
	TestFalse(TEXT("(4,2) wrong row not covered"), ASimGrid_FogCarrier::RunArrayContains(Arr, FSeam_CellCoord(4, 2)));

	// The single-cell run matches only its exact cell.
	TestTrue(TEXT("(0,0) single-cell run covered"), ASimGrid_FogCarrier::RunArrayContains(Arr, FSeam_CellCoord(0, 0)));
	TestFalse(TEXT("(1,0) adjacent to single-cell not covered"), ASimGrid_FogCarrier::RunArrayContains(Arr, FSeam_CellCoord(1, 0)));

	// Empty array covers nothing.
	FSimGrid_FogRunArray Empty;
	TestFalse(TEXT("empty array covers nothing"), ASimGrid_FogCarrier::RunArrayContains(Empty, FSeam_CellCoord(0, 0)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDPFogEnsureCellTest,
	"DesignPatterns.SimGrid.Fog.EnsureCellInRunArray",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDPFogEnsureCellTest::RunTest(const FString& /*Parameters*/)
{
	FSimGrid_FogRunArray Arr;

	// First insert of a fresh cell: a new single-cell run is added; returns true.
	const bool bAdded = ASimGrid_FogCarrier::EnsureCellInRunArray(Arr, FSeam_CellCoord(5, 2), /*bVisible*/ true, nullptr);
	TestTrue(TEXT("fresh cell inserted"), bAdded);
	TestEqual(TEXT("one entry after first insert"), Arr.Entries.Num(), 1);
	TestTrue(TEXT("cell is now covered"), ASimGrid_FogCarrier::RunArrayContains(Arr, FSeam_CellCoord(5, 2)));

	// The inserted run is exactly one cell wide with the requested visibility.
	TestEqual(TEXT("run StartX == EndX == 5"), Arr.Entries[0].StartX, 5);
	TestEqual(TEXT("run EndX == 5"), Arr.Entries[0].EndX, 5);
	TestEqual(TEXT("run RowY == 2"), Arr.Entries[0].RowY, 2);
	TestTrue(TEXT("run is currently visible"), Arr.Entries[0].bCurrentlyVisible);

	// Re-ensuring the SAME cell is a no-op (explored cells are permanent): returns false, no new entry.
	const bool bAgain = ASimGrid_FogCarrier::EnsureCellInRunArray(Arr, FSeam_CellCoord(5, 2), /*bVisible*/ false, nullptr);
	TestFalse(TEXT("re-ensuring same cell is a no-op"), bAgain);
	TestEqual(TEXT("still one entry"), Arr.Entries.Num(), 1);

	// A different cell adds a second run.
	const bool bAdded2 = ASimGrid_FogCarrier::EnsureCellInRunArray(Arr, FSeam_CellCoord(6, 2), true, nullptr);
	TestTrue(TEXT("different cell inserted"), bAdded2);
	TestEqual(TEXT("two entries"), Arr.Entries.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDPFogRemoveCellTest,
	"DesignPatterns.SimGrid.Fog.RemoveCellFromRunArray",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDPFogRemoveCellTest::RunTest(const FString& /*Parameters*/)
{
	using namespace DPFogTestUtil;
	FSimGrid_FogRunArray Arr;
	ASimGrid_FogCarrier::EnsureCellInRunArray(Arr, FSeam_CellCoord(1, 1), true, nullptr);
	ASimGrid_FogCarrier::EnsureCellInRunArray(Arr, FSeam_CellCoord(2, 1), true, nullptr);

	// Remove an existing single-cell run.
	const bool bRemoved = ASimGrid_FogCarrier::RemoveCellFromRunArray(Arr, FSeam_CellCoord(1, 1), nullptr);
	TestTrue(TEXT("existing cell removed"), bRemoved);
	TestFalse(TEXT("removed cell no longer covered"), ASimGrid_FogCarrier::RunArrayContains(Arr, FSeam_CellCoord(1, 1)));
	TestTrue(TEXT("other cell still covered"), ASimGrid_FogCarrier::RunArrayContains(Arr, FSeam_CellCoord(2, 1)));
	TestEqual(TEXT("one entry remains"), Arr.Entries.Num(), 1);

	// Removing an absent cell returns false and changes nothing.
	const bool bNoop = ASimGrid_FogCarrier::RemoveCellFromRunArray(Arr, FSeam_CellCoord(9, 9), nullptr);
	TestFalse(TEXT("removing absent cell returns false"), bNoop);
	TestEqual(TEXT("still one entry"), Arr.Entries.Num(), 1);

	// Removing a cell inside a multi-cell run drops the WHOLE run (documented: removes any covering run).
	FSimGrid_FogRunArray Multi;
	AddRun(Multi, /*RowY*/ 4, /*StartX*/ 0, /*EndX*/ 3, true);
	const bool bRemMulti = ASimGrid_FogCarrier::RemoveCellFromRunArray(Multi, FSeam_CellCoord(2, 4), nullptr);
	TestTrue(TEXT("cell inside multi-cell run removes the run"), bRemMulti);
	TestEqual(TEXT("multi-cell run array now empty"), Multi.Entries.Num(), 0);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
