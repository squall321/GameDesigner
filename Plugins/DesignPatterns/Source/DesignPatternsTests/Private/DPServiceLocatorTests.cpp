// Copyright DesignPatterns plugin. All Rights Reserved.

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Services/DPServiceTypes.h"
#include "GameplayTagsManager.h"

namespace DPServiceTestUtil
{
	static FGameplayTag Tag(const TCHAR* Name)
	{
		return UGameplayTagsManager::Get().RequestGameplayTag(FName(Name), /*ErrorIfNotFound=*/false);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDPServiceStrongRegisterResolveTest,
	"DesignPatterns.ServiceLocator.StrongRegisterResolveUnregister",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDPServiceStrongRegisterResolveTest::RunTest(const FString& /*Parameters*/)
{
	UDP_ServiceLocatorSubsystem* Loc = NewObject<UDP_ServiceLocatorSubsystem>();
	const FGameplayTag Key = DPServiceTestUtil::Tag(TEXT("DP.Service"));
	if (!Key.IsValid())
	{
		AddWarning(TEXT("DP.Service tag not registered; skipping."));
		return true;
	}

	UObject* Provider = NewObject<UDP_ServiceLocatorSubsystem>(); // any UObject as a stand-in provider

	TestFalse(TEXT("not registered initially"), Loc->IsRegistered(Key));

	const bool bReg = Loc->RegisterService(Key, Provider, EDP_ServiceLifetime::StrongOwned);
	TestTrue(TEXT("registration succeeds"), bReg);
	TestTrue(TEXT("now registered"), Loc->IsRegistered(Key));
	TestEqual(TEXT("resolve returns the same provider"), Loc->ResolveService(Key), Provider);

	// Double-registration without override must fail.
	UObject* Provider2 = NewObject<UDP_ServiceLocatorSubsystem>();
	const bool bDup = Loc->RegisterService(Key, Provider2, EDP_ServiceLifetime::StrongOwned, /*bAllowOverride=*/false);
	TestFalse(TEXT("duplicate registration rejected"), bDup);
	TestEqual(TEXT("original provider unchanged"), Loc->ResolveService(Key), Provider);

	// Override allowed when requested.
	const bool bOverride = Loc->RegisterService(Key, Provider2, EDP_ServiceLifetime::StrongOwned, /*bAllowOverride=*/true);
	TestTrue(TEXT("override registration succeeds"), bOverride);
	TestEqual(TEXT("resolve returns the new provider"), Loc->ResolveService(Key), Provider2);

	// Unregister.
	TestTrue(TEXT("unregister succeeds"), Loc->UnregisterService(Key));
	TestFalse(TEXT("no longer registered"), Loc->IsRegistered(Key));
	TestNull(TEXT("resolve returns null after unregister"), Loc->ResolveService(Key));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDPServiceWeakInvalidateTest,
	"DesignPatterns.ServiceLocator.WeakObservedInvalidatesOnGC",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDPServiceWeakInvalidateTest::RunTest(const FString& /*Parameters*/)
{
	UDP_ServiceLocatorSubsystem* Loc = NewObject<UDP_ServiceLocatorSubsystem>();
	// Root the locator so it survives the forced GC below.
	Loc->AddToRoot();

	const FGameplayTag Key = DPServiceTestUtil::Tag(TEXT("DP.Service"));
	if (!Key.IsValid())
	{
		Loc->RemoveFromRoot();
		AddWarning(TEXT("DP.Service tag not registered; skipping."));
		return true;
	}

	// A provider with no other references: weak-observed registration must NOT keep it alive.
	UObject* Provider = NewObject<UDP_ServiceLocatorSubsystem>();
	Loc->RegisterService(Key, Provider, EDP_ServiceLifetime::WeakObserved);
	TestEqual(TEXT("weak provider resolves before GC"), Loc->ResolveService(Key), Provider);

	// Drop the only strong reference and force a GC.
	Provider = nullptr;
	CollectGarbage(RF_NoFlags, /*bFullPurge=*/true);

	// After GC the weak entry must report invalidated (resolve null / not registered).
	TestNull(TEXT("weak provider resolves to null after GC"), Loc->ResolveService(Key));

	Loc->RemoveFromRoot();
	return true;
}

#endif // WITH_AUTOMATION_TESTS
