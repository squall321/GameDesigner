// Copyright DesignPatterns plugin. All Rights Reserved.

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "GameplayTagsManager.h"

// These tests exercise the bus's pure routing logic, which needs no live world: a NewObject
// instance routes synchronous Broadcasts to ListenNative handlers. The deferred queue (which uses
// the core ticker registered in Initialize) is intentionally not exercised here.

namespace DPBusTestUtil
{
	static FGameplayTag Tag(const TCHAR* Name)
	{
		return UGameplayTagsManager::Get().RequestGameplayTag(FName(Name), /*ErrorIfNotFound=*/false);
	}

	// A throwaway owner whose lifetime keeps a listener alive.
	static UObject* NewOwner()
	{
		return NewObject<UDP_MessageBusSubsystem>(); // any UObject works as an owner; reuse the type
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDPBusHierarchyMatchTest,
	"DesignPatterns.MessageBus.ParentListenerReceivesChild",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDPBusHierarchyMatchTest::RunTest(const FString& /*Parameters*/)
{
	UDP_MessageBusSubsystem* Bus = NewObject<UDP_MessageBusSubsystem>();
	TestNotNull(TEXT("bus created"), Bus);

	const FGameplayTag Parent = DPBusTestUtil::Tag(TEXT("DP.Bus"));
	const FGameplayTag Child = DPBusTestUtil::Tag(TEXT("DP.Bus.Combat"));
	if (!Parent.IsValid() || !Child.IsValid())
	{
		AddWarning(TEXT("DP.Bus / DP.Bus.Combat tags not registered; skipping hierarchy assertion."));
		return true;
	}

	UObject* Owner = DPBusTestUtil::NewOwner();
	int32 Hits = 0;
	Bus->ListenNative(Parent, [&Hits](const FDP_Message&){ ++Hits; }, Owner, EDP_MessageMatch::ExactOrChild);

	// Broadcasting on the child should reach the parent listener (ExactOrChild).
	Bus->BroadcastPayload(Child, FInstancedStruct(), nullptr);
	TestEqual(TEXT("parent listener received child broadcast"), Hits, 1);

	// Broadcasting on the parent itself should also reach it.
	Bus->BroadcastPayload(Parent, FInstancedStruct(), nullptr);
	TestEqual(TEXT("parent listener received parent broadcast"), Hits, 2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDPBusExactMatchTest,
	"DesignPatterns.MessageBus.ExactMatchIgnoresChild",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDPBusExactMatchTest::RunTest(const FString& /*Parameters*/)
{
	UDP_MessageBusSubsystem* Bus = NewObject<UDP_MessageBusSubsystem>();
	const FGameplayTag Parent = DPBusTestUtil::Tag(TEXT("DP.Bus"));
	const FGameplayTag Child = DPBusTestUtil::Tag(TEXT("DP.Bus.Combat"));
	if (!Parent.IsValid() || !Child.IsValid())
	{
		AddWarning(TEXT("tags not registered; skipping."));
		return true;
	}

	UObject* Owner = DPBusTestUtil::NewOwner();
	int32 Hits = 0;
	Bus->ListenNative(Parent, [&Hits](const FDP_Message&){ ++Hits; }, Owner, EDP_MessageMatch::Exact);

	// Exact listener on the parent must NOT fire for a child-tag broadcast.
	Bus->BroadcastPayload(Child, FInstancedStruct(), nullptr);
	TestEqual(TEXT("exact listener ignores child broadcast"), Hits, 0);

	// But fires for the exact tag.
	Bus->BroadcastPayload(Parent, FInstancedStruct(), nullptr);
	TestEqual(TEXT("exact listener fires for exact tag"), Hits, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDPBusOwnerRemovalTest,
	"DesignPatterns.MessageBus.StopListeningForOwnerAndCount",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDPBusOwnerRemovalTest::RunTest(const FString& /*Parameters*/)
{
	UDP_MessageBusSubsystem* Bus = NewObject<UDP_MessageBusSubsystem>();
	const FGameplayTag Channel = DPBusTestUtil::Tag(TEXT("DP.Bus"));
	if (!Channel.IsValid())
	{
		AddWarning(TEXT("DP.Bus tag not registered; skipping."));
		return true;
	}

	TestEqual(TEXT("starts with no listeners"), Bus->GetListenerCount(), 0);

	UObject* OwnerA = DPBusTestUtil::NewOwner();
	UObject* OwnerB = DPBusTestUtil::NewOwner();
	Bus->ListenNative(Channel, [](const FDP_Message&){}, OwnerA);
	Bus->ListenNative(Channel, [](const FDP_Message&){}, OwnerA);
	Bus->ListenNative(Channel, [](const FDP_Message&){}, OwnerB);
	TestEqual(TEXT("three listeners registered"), Bus->GetListenerCount(), 3);

	Bus->StopListeningForOwner(OwnerA);
	TestEqual(TEXT("removing owner A drops its two listeners"), Bus->GetListenerCount(), 1);

	Bus->StopListeningForOwner(OwnerB);
	TestEqual(TEXT("removing owner B drops the last listener"), Bus->GetListenerCount(), 0);

	return true;
}

#endif // WITH_AUTOMATION_TESTS
