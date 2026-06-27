// Copyright DesignPatterns plugin. All Rights Reserved.

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "GameplayTagsManager.h"

#include "AutoTile/SimGrid_AutoTileLib.h"
#include "Grid/Seam_TileProviderRead.h"
#include "Grid/Seam_GridCoord.h"
#include "World/SimGrid_CoordTypes.h"

#include "DPSimGridAutoTileTests.generated.h"

// A pure in-memory grid implementing the read seam, for testing USimGrid_AutoTileLib without a world.
// Cells in SetCells are "Set" with the stored tile-type tag; everything else is Unknown. With no data
// registry available (transient package), USimGrid_AutoTileLib::CellCategory falls back to the exact
// tile-type tag, so connectivity here is exact-tag equality — the documented fallback path.
UCLASS(Transient, NotBlueprintable)
class UDPTest_MockTileGrid : public UObject, public ISeam_TileProviderRead
{
	GENERATED_BODY()

public:
	TMap<FSeam_CellCoord, FGameplayTag> SetCells;

	void Set(int32 X, int32 Y, const FGameplayTag& Tag) { SetCells.Add(FSeam_CellCoord(X, Y), Tag); }

	// --- ISeam_TileProviderRead ---
	virtual FSeam_CellSnapshot GetCellSnapshot_Implementation(const FSeam_CellCoord& Cell) const override
	{
		FSeam_CellSnapshot Snap;
		if (const FGameplayTag* Tag = SetCells.Find(Cell))
		{
			Snap.KnownState = ESeam_KnownState::Set;
			Snap.TileTypeTag = *Tag;
		}
		else
		{
			Snap.KnownState = ESeam_KnownState::Unknown;
		}
		return Snap;
	}
	virtual bool IsValidCell_Implementation(const FSeam_CellCoord& /*Cell*/) const override { return true; }
	virtual FSeam_CellCoord WorldToCell_Implementation(const FVector& /*W*/) const override { return FSeam_CellCoord(); }
	virtual FVector CellToWorld_Implementation(const FSeam_CellCoord& /*C*/, bool /*bCenter*/) const override { return FVector::ZeroVector; }
	virtual float GetCellSize_Implementation() const override { return 100.f; }
};

namespace DPAutoTileTestUtil
{
	// Two distinct, already-registered native tags (guaranteed present in the project). Connectivity
	// only compares tag equality, so any two distinct valid tags serve as two distinct tile types.
	static FGameplayTag TagA() { return UGameplayTagsManager::Get().RequestGameplayTag(FName(TEXT("DP.Bus")), false); }
	static FGameplayTag TagB() { return UGameplayTagsManager::Get().RequestGameplayTag(FName(TEXT("DP.Service")), false); }

	static TScriptInterface<ISeam_TileProviderRead> AsSeam(UDPTest_MockTileGrid* Grid)
	{
		TScriptInterface<ISeam_TileProviderRead> S;
		S.SetObject(Grid);
		S.SetInterface(Cast<ISeam_TileProviderRead>(Grid));
		return S;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDPAutoTileBitmaskCardinalTest,
	"DesignPatterns.SimGrid.AutoTile.BitmaskCardinals",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDPAutoTileBitmaskCardinalTest::RunTest(const FString& /*Parameters*/)
{
	using namespace DPAutoTileTestUtil;
	const FGameplayTag A = TagA();
	if (!A.IsValid())
	{
		AddWarning(TEXT("DP.Bus tag not registered in this context; skipping auto-tile bitmask test."));
		return true;
	}

	UDPTest_MockTileGrid* Grid = NewObject<UDPTest_MockTileGrid>(GetTransientPackage());
	// Centre at (0,0) plus its +X and +Y neighbours, same tag. -X and -Y are Unknown (gaps).
	Grid->Set(0, 0, A);
	Grid->Set(1, 0, A);   // +X
	Grid->Set(0, 1, A);   // +Y
	const TScriptInterface<ISeam_TileProviderRead> S = AsSeam(Grid);

	// Four-connected: +X(1) | +Y(4) = 5. -X(2), -Y(8) are gaps.
	const int32 Mask4 = USimGrid_AutoTileLib::ComputeAdjacencyBitmask(S, FSeam_CellCoord(0, 0), ESimGrid_Adjacency::Four);
	TestEqual(TEXT("cardinal mask is +X|+Y = 5"), Mask4, 5);

	// A different-tag neighbour does NOT connect.
	Grid->Set(-1, 0, TagB());  // -X with a different tag
	const int32 Mask4b = USimGrid_AutoTileLib::ComputeAdjacencyBitmask(S, FSeam_CellCoord(0, 0), ESimGrid_Adjacency::Four);
	TestEqual(TEXT("different-tag neighbour does not connect (still 5)"), Mask4b, 5);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDPAutoTileBlobRuleTest,
	"DesignPatterns.SimGrid.AutoTile.BlobRuleDiagonals",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDPAutoTileBlobRuleTest::RunTest(const FString& /*Parameters*/)
{
	using namespace DPAutoTileTestUtil;
	const FGameplayTag A = TagA();
	if (!A.IsValid())
	{
		AddWarning(TEXT("DP.Bus tag not registered; skipping blob-rule test."));
		return true;
	}

	// A "plus" shape: centre + 4 cardinals set, NO diagonals set.
	UDPTest_MockTileGrid* Plus = NewObject<UDPTest_MockTileGrid>(GetTransientPackage());
	Plus->Set(0, 0, A);
	Plus->Set(1, 0, A); Plus->Set(-1, 0, A); Plus->Set(0, 1, A); Plus->Set(0, -1, A);
	const int32 PlusMask = USimGrid_AutoTileLib::ComputeAdjacencyBitmask(AsSeam(Plus), FSeam_CellCoord(0, 0), ESimGrid_Adjacency::Eight);
	// All 4 cardinals connect (15); no diagonal cell is Set so no diagonal bit can be set even though
	// flanking cardinals connect — the blob rule also requires the diagonal cell itself to connect.
	TestEqual(TEXT("plus shape: cardinals only (15), no diagonals"), PlusMask, 15);

	// A full 3x3 block: every neighbour and diagonal set -> all 8 bits = 255.
	UDPTest_MockTileGrid* Block = NewObject<UDPTest_MockTileGrid>(GetTransientPackage());
	for (int32 Y = -1; Y <= 1; ++Y)
	{
		for (int32 X = -1; X <= 1; ++X)
		{
			Block->Set(X, Y, A);
		}
	}
	const int32 BlockMask = USimGrid_AutoTileLib::ComputeAdjacencyBitmask(AsSeam(Block), FSeam_CellCoord(0, 0), ESimGrid_Adjacency::Eight);
	TestEqual(TEXT("full 3x3 block: all 8 bits set (255)"), BlockMask, 255);

	// Diagonal-only (a diagonal Set but its flanking cardinals are gaps): blob rule suppresses the diagonal bit.
	UDPTest_MockTileGrid* DiagOnly = NewObject<UDPTest_MockTileGrid>(GetTransientPackage());
	DiagOnly->Set(0, 0, A);
	DiagOnly->Set(1, 1, A); // +X+Y diagonal, but +X and +Y cardinals are gaps
	const int32 DiagMask = USimGrid_AutoTileLib::ComputeAdjacencyBitmask(AsSeam(DiagOnly), FSeam_CellCoord(0, 0), ESimGrid_Adjacency::Eight);
	TestEqual(TEXT("lone diagonal suppressed by blob rule (0)"), DiagMask, 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDPAutoTileMarchingSquaresTest,
	"DesignPatterns.SimGrid.AutoTile.MarchingSquares",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDPAutoTileMarchingSquaresTest::RunTest(const FString& /*Parameters*/)
{
	using namespace DPAutoTileTestUtil;
	const FGameplayTag A = TagA();
	if (!A.IsValid())
	{
		AddWarning(TEXT("DP.Bus tag not registered; skipping marching-squares test."));
		return true;
	}

	UDPTest_MockTileGrid* Grid = NewObject<UDPTest_MockTileGrid>(GetTransientPackage());
	const TScriptInterface<ISeam_TileProviderRead> S = AsSeam(Grid);
	const FSeam_CellCoord Corner(0, 0);

	// Empty corner -> case 0.
	TestEqual(TEXT("empty corner -> 0"), USimGrid_AutoTileLib::MarchingSquaresCase(S, Corner, A), 0);

	// bit1=Corner(0,0), bit2=+X(1,0), bit4=+X+Y(1,1), bit8=+Y(0,1).
	Grid->Set(0, 0, A);
	TestEqual(TEXT("corner only -> 1"), USimGrid_AutoTileLib::MarchingSquaresCase(S, Corner, A), 1);
	Grid->Set(1, 0, A);
	TestEqual(TEXT("corner + +X -> 3"), USimGrid_AutoTileLib::MarchingSquaresCase(S, Corner, A), 3);
	Grid->Set(0, 1, A);
	TestEqual(TEXT("corner + +X + +Y -> 11"), USimGrid_AutoTileLib::MarchingSquaresCase(S, Corner, A), 11);
	Grid->Set(1, 1, A);
	TestEqual(TEXT("full 2x2 -> 15"), USimGrid_AutoTileLib::MarchingSquaresCase(S, Corner, A), 15);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDPAutoTileRegionLabelingTest,
	"DesignPatterns.SimGrid.AutoTile.RegionLabeling",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDPAutoTileRegionLabelingTest::RunTest(const FString& /*Parameters*/)
{
	using namespace DPAutoTileTestUtil;
	const FGameplayTag A = TagA();
	if (!A.IsValid())
	{
		AddWarning(TEXT("DP.Bus tag not registered; skipping region-labeling test."));
		return true;
	}

	// Two separated blobs of the same tag inside a 0..5 window:
	//   blob 1: (0,0),(1,0),(0,1)         -> size 3
	//   blob 2: (4,4),(5,4)               -> size 2
	UDPTest_MockTileGrid* Grid = NewObject<UDPTest_MockTileGrid>(GetTransientPackage());
	Grid->Set(0, 0, A); Grid->Set(1, 0, A); Grid->Set(0, 1, A);
	Grid->Set(4, 4, A); Grid->Set(5, 4, A);
	const TScriptInterface<ISeam_TileProviderRead> S = AsSeam(Grid);

	// MatchCategory invalid => "any Set cell" matches; 4-connected so the two blobs stay separate.
	const FSimGrid_RegionLabeling R = USimGrid_AutoTileLib::LabelConnectedRegions(
		S, FSeam_CellCoord(0, 0), FSeam_CellCoord(5, 5), FGameplayTag(), ESimGrid_Adjacency::Four);

	TestFalse(TEXT("labeling not truncated"), R.bTruncated);
	TestEqual(TEXT("two regions found"), R.RegionCount, 2);
	TestEqual(TEXT("region sizes recorded for both"), R.RegionSizes.Num(), 2);

	// The two seed cells belong to different regions; total labeled cells == 5.
	const int32* Id1 = R.RegionIdByCell.Find(FSeam_CellCoord(0, 0));
	const int32* Id2 = R.RegionIdByCell.Find(FSeam_CellCoord(4, 4));
	TestNotNull(TEXT("blob-1 seed labeled"), Id1);
	TestNotNull(TEXT("blob-2 seed labeled"), Id2);
	if (Id1 && Id2)
	{
		TestNotEqual(TEXT("the two blobs are distinct regions"), *Id1, *Id2);
		TestEqual(TEXT("blob containing (0,0) has size 3"), R.RegionSizes[*Id1], 3);
		TestEqual(TEXT("blob containing (4,4) has size 2"), R.RegionSizes[*Id2], 2);
	}
	TestEqual(TEXT("total labeled cells == 5"), R.RegionIdByCell.Num(), 5);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
