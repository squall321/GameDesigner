// Copyright DesignPatterns plugin. All Rights Reserved.

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "GameplayTagsManager.h"

#include "Stats/RPG_StatsComponent.h"
#include "Stats/Seam_StatMod.h"
#include "Stats/Seam_StatModifierSink.h"
#include "Net/Seam_NetValue.h"

// The RPG stat aggregation folds modifiers as (base + sum(Additive)) * prod(1 + Multiplicative), unless an
// Override is present (override wins). The LOCAL-DERIVED path — SetDerivedModifierGroup — runs with NO
// authority guard and NO owner (it must fold identically on server and clients, or equipment/affix/set
// modifiers desync). That makes it testable on a bare component: NewObject a URPG_StatsComponent in the
// transient package and drive it through the ISeam_StatModifierSink seam. (Base attributes can't be seeded
// here — SetBaseAttribute is owner+authority-gated — so these tests fold from base 0, which is the math
// that matters for the derived-modifier desync risk.)

namespace DPStatTestUtil
{
	static FGameplayTag Attr() { return UGameplayTagsManager::Get().RequestGameplayTag(FName(TEXT("DP.Bus")), false); }
	static FGameplayTag Source(const TCHAR* N) { return UGameplayTagsManager::Get().RequestGameplayTag(FName(N), false); }

	static FSeam_StatMod Mod(const FGameplayTag& AttributeTag, ERPG_StatModOp Op, float Magnitude)
	{
		FSeam_StatMod M;
		M.AttributeTag = AttributeTag;
		M.Op = static_cast<uint8>(Op);
		M.Magnitude = FSeam_NetValue::MakeFloat(Magnitude);
		return M;
	}

	static URPG_StatsComponent* NewStats()
	{
		return NewObject<URPG_StatsComponent>(GetTransientPackage());
	}

	// Apply a derived-modifier group via the seam (the LOCAL-DERIVED, no-authority path).
	static void SetGroup(URPG_StatsComponent* Stats, const FGameplayTag& SourceTag, const TArray<FSeam_StatMod>& Mods)
	{
		ISeam_StatModifierSink::Execute_SetDerivedModifierGroup(Stats, SourceTag, Mods);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDPStatsAdditiveTest,
	"DesignPatterns.RPG.Stats.AdditiveFold",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDPStatsAdditiveTest::RunTest(const FString& /*Parameters*/)
{
	using namespace DPStatTestUtil;
	const FGameplayTag A = Attr();
	if (!A.IsValid())
	{
		AddWarning(TEXT("DP.Bus tag not registered; skipping stat additive test."));
		return true;
	}
	URPG_StatsComponent* S = NewStats();

	// Two additive modifiers from one source fold to their sum (base is 0 on a bare component).
	SetGroup(S, Source(TEXT("DP.Service")), {
		Mod(A, ERPG_StatModOp::Additive, 10.f),
		Mod(A, ERPG_StatModOp::Additive, 5.f),
	});
	TestEqual(TEXT("two additives sum to 15"), S->GetAttributeValue(A), 15.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDPStatsMultiplicativeTest,
	"DesignPatterns.RPG.Stats.MultiplicativeFold",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDPStatsMultiplicativeTest::RunTest(const FString& /*Parameters*/)
{
	using namespace DPStatTestUtil;
	const FGameplayTag A = Attr();
	if (!A.IsValid())
	{
		AddWarning(TEXT("DP.Bus tag not registered; skipping stat multiplicative test."));
		return true;
	}
	URPG_StatsComponent* S = NewStats();

	// (0 + 10) * (1 + 0.5) = 15 — multiplicative applies AFTER additive.
	SetGroup(S, Source(TEXT("DP.Service")), {
		Mod(A, ERPG_StatModOp::Additive, 10.f),
		Mod(A, ERPG_StatModOp::Multiplicative, 0.5f),
	});
	TestEqual(TEXT("(0+10)*1.5 == 15"), S->GetAttributeValue(A), 15.f);

	// Two multipliers STACK multiplicatively: 10 * 1.5 * 1.5 = 22.5.
	SetGroup(S, Source(TEXT("DP.Service")), {
		Mod(A, ERPG_StatModOp::Additive, 10.f),
		Mod(A, ERPG_StatModOp::Multiplicative, 0.5f),
		Mod(A, ERPG_StatModOp::Multiplicative, 0.5f),
	});
	TestEqual(TEXT("10 * 1.5 * 1.5 == 22.5"), S->GetAttributeValue(A), 22.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDPStatsOverrideTest,
	"DesignPatterns.RPG.Stats.OverrideWins",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDPStatsOverrideTest::RunTest(const FString& /*Parameters*/)
{
	using namespace DPStatTestUtil;
	const FGameplayTag A = Attr();
	if (!A.IsValid())
	{
		AddWarning(TEXT("DP.Bus tag not registered; skipping stat override test."));
		return true;
	}
	URPG_StatsComponent* S = NewStats();

	// An override discards additive/multiplicative entirely.
	SetGroup(S, Source(TEXT("DP.Service")), {
		Mod(A, ERPG_StatModOp::Additive, 100.f),
		Mod(A, ERPG_StatModOp::Multiplicative, 9.f),
		Mod(A, ERPG_StatModOp::Override, 7.f),
	});
	TestEqual(TEXT("override discards add/mult -> 7"), S->GetAttributeValue(A), 7.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDPStatsGroupReplaceTest,
	"DesignPatterns.RPG.Stats.GroupReplaceAndRemove",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDPStatsGroupReplaceTest::RunTest(const FString& /*Parameters*/)
{
	using namespace DPStatTestUtil;
	const FGameplayTag A = Attr();
	if (!A.IsValid())
	{
		AddWarning(TEXT("DP.Bus tag not registered; skipping stat group-replace test."));
		return true;
	}
	URPG_StatsComponent* S = NewStats();
	const FGameplayTag Src = Source(TEXT("DP.Service"));

	// First group: +10.
	SetGroup(S, Src, { Mod(A, ERPG_StatModOp::Additive, 10.f) });
	TestEqual(TEXT("group gives +10"), S->GetAttributeValue(A), 10.f);

	// Re-setting the SAME source replaces the group atomically (not additive on top): now +3, not +13.
	SetGroup(S, Src, { Mod(A, ERPG_StatModOp::Additive, 3.f) });
	TestEqual(TEXT("re-set replaces group -> 3 (not 13)"), S->GetAttributeValue(A), 3.f);

	// Setting the source to an EMPTY group removes its contribution entirely -> back to base 0.
	SetGroup(S, Src, TArray<FSeam_StatMod>());
	TestEqual(TEXT("empty group removes contribution -> 0"), S->GetAttributeValue(A), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDPStatsMultiSourceTest,
	"DesignPatterns.RPG.Stats.MultipleSourcesFold",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDPStatsMultiSourceTest::RunTest(const FString& /*Parameters*/)
{
	using namespace DPStatTestUtil;
	const FGameplayTag A = Attr();
	if (!A.IsValid())
	{
		AddWarning(TEXT("DP.Bus tag not registered; skipping stat multi-source test."));
		return true;
	}
	URPG_StatsComponent* S = NewStats();

	// Two distinct sources (e.g. two equipped items) both contribute; removing one keeps the other.
	SetGroup(S, Source(TEXT("DP.Service")), { Mod(A, ERPG_StatModOp::Additive, 10.f) });
	SetGroup(S, Source(TEXT("DP.Bus")),     { Mod(A, ERPG_StatModOp::Additive, 4.f) });
	TestEqual(TEXT("two sources sum to 14"), S->GetAttributeValue(A), 14.f);

	// Removing one source leaves the other intact.
	SetGroup(S, Source(TEXT("DP.Service")), TArray<FSeam_StatMod>());
	TestEqual(TEXT("removing one source leaves 4"), S->GetAttributeValue(A), 4.f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
