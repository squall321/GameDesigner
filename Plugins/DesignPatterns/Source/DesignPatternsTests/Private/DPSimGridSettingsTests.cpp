// Copyright DesignPatterns plugin. All Rights Reserved.

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "Settings/SimGrid_DeveloperSettings.h"

// SimGrid once shipped TWO distinct classes both named USimGrid_DeveloperSettings (a duplicate-symbol
// collision): a root-level "query caps" class and a Settings/ "layout" class. They were merged into the
// single canonical Settings/ class and the root pair deleted. These tests regression-guard that merge:
//   - the merged class carries BOTH the layout fields AND the former query/placement caps;
//   - MaxFloodFillCells kept the LARGER default (8192 from the old query class), so query behaviour is
//     unchanged — if anyone re-lowers it to the old layout-class default (4096), this fails;
//   - GetSafeChunkSize() still clamps each axis to >= 1.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDPSimGridSettingsMergeFieldsTest,
	"DesignPatterns.SimGrid.Settings.MergedFields",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDPSimGridSettingsMergeFieldsTest::RunTest(const FString& /*Parameters*/)
{
	const USimGrid_DeveloperSettings* CDO = GetDefault<USimGrid_DeveloperSettings>();
	TestNotNull(TEXT("SimGrid settings CDO resolves"), CDO);
	if (!CDO)
	{
		return false;
	}

	// Layout fields (from the original Settings/ class) are present and sane.
	TestTrue(TEXT("DefaultCellSize positive"), CDO->DefaultCellSize > 0.f);
	TestTrue(TEXT("DefaultChunkSize axes positive"), CDO->DefaultChunkSize.X > 0 && CDO->DefaultChunkSize.Y > 0);
	TestTrue(TEXT("MaxQueryRadius positive"), CDO->MaxQueryRadius > 0);
	TestTrue(TEXT("MaxLineLength positive"), CDO->MaxLineLength > 0);

	// Merged-in query/placement caps (from the deleted root class) are present and sane. Reading these
	// members is itself the structural guard that the merge carried them over.
	TestTrue(TEXT("MaxQueryRadiusCells positive"), CDO->MaxQueryRadiusCells > 0);
	TestTrue(TEXT("MaxRegionCells positive"), CDO->MaxRegionCells > 0);
	TestTrue(TEXT("MaxLineCells positive"), CDO->MaxLineCells > 0);
	TestTrue(TEXT("MaxFootprintCells positive"), CDO->MaxFootprintCells > 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDPSimGridFloodFillDefaultTest,
	"DesignPatterns.SimGrid.Settings.FloodFillDefaultPreserved",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDPSimGridFloodFillDefaultTest::RunTest(const FString& /*Parameters*/)
{
	// A FRESH instance carries the compiled-in default, independent of any project .ini override of the CDO.
	const USimGrid_DeveloperSettings* Fresh = NewObject<USimGrid_DeveloperSettings>(GetTransientPackage());
	TestNotNull(TEXT("fresh SimGrid settings constructed"), Fresh);
	if (!Fresh)
	{
		return false;
	}

	// CRITICAL: the merge must keep the larger flood-fill cap (8192 from the old query class), NOT the
	// layout class's old 4096. Re-lowering this silently halves query reach — guard it here.
	TestEqual(TEXT("MaxFloodFillCells default preserved at 8192"), Fresh->MaxFloodFillCells, 8192);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDPSimGridSafeChunkSizeTest,
	"DesignPatterns.SimGrid.Settings.SafeChunkSizeClamps",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDPSimGridSafeChunkSizeTest::RunTest(const FString& /*Parameters*/)
{
	USimGrid_DeveloperSettings* S = NewObject<USimGrid_DeveloperSettings>(GetTransientPackage());
	TestNotNull(TEXT("settings instance constructed"), S);
	if (!S)
	{
		return false;
	}

	// A degenerate (0,0) chunk size must clamp to (1,1) so chunk math never divides by zero.
	S->DefaultChunkSize = FIntPoint(0, 0);
	TestTrue(TEXT("(0,0) chunk clamps to (1,1)"), S->GetSafeChunkSize() == FIntPoint(1, 1));

	// A negative axis clamps too; the other axis is preserved.
	S->DefaultChunkSize = FIntPoint(-4, 8);
	const FIntPoint Safe = S->GetSafeChunkSize();
	TestEqual(TEXT("negative X clamps to 1"), Safe.X, 1);
	TestEqual(TEXT("valid Y preserved"), Safe.Y, 8);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
