// Copyright DesignPatterns plugin. All Rights Reserved.

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Net/Seam_NetValue.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "GameplayTagsManager.h"

// FSeam_NetValue is the closed net-friendly variant that replaces replicating a raw FInstancedStruct.
// These tests exercise its pure logic with no world: type discrimination, equality, a NetSerialize
// write/read roundtrip per active type, and FInstancedStruct conversion.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDPNetValueTypeAndEqualityTest,
	"DesignPatterns.Seams.NetValue.TypeAndEquality",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDPNetValueTypeAndEqualityTest::RunTest(const FString& /*Parameters*/)
{
	const FSeam_NetValue B = FSeam_NetValue::MakeBool(true);
	const FSeam_NetValue I = FSeam_NetValue::MakeInt(42);
	const FSeam_NetValue F = FSeam_NetValue::MakeFloat(3.5);
	const FSeam_NetValue V = FSeam_NetValue::MakeVector(FVector(1, 2, 3));

	TestEqual(TEXT("bool type"), B.Type, ESeam_NetValueType::Bool);
	TestEqual(TEXT("int type"), I.Type, ESeam_NetValueType::Int);
	TestTrue(TEXT("bool is set"), B.IsSet());
	TestFalse(TEXT("default is not set"), FSeam_NetValue().IsSet());

	// Equality is type-sensitive.
	TestTrue(TEXT("equal bools compare equal"), B == FSeam_NetValue::MakeBool(true));
	TestFalse(TEXT("different bools differ"), B == FSeam_NetValue::MakeBool(false));
	TestFalse(TEXT("different types differ"), B == I);
	TestTrue(TEXT("equal vectors compare equal"), V == FSeam_NetValue::MakeVector(FVector(1, 2, 3)));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDPNetValueSerializeRoundtripTest,
	"DesignPatterns.Seams.NetValue.SerializeRoundtrip",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDPNetValueSerializeRoundtripTest::RunTest(const FString& /*Parameters*/)
{
	auto Roundtrip = [this](const FSeam_NetValue& In, const TCHAR* Label)
	{
		TArray<uint8> Bytes;
		{
			FMemoryWriter Writer(Bytes, /*bIsPersistent*/ true);
			FSeam_NetValue Mutable = In;
			bool bOk = false;
			Mutable.NetSerialize(Writer, nullptr, bOk);
			TestTrue(FString::Printf(TEXT("%s write ok"), Label), bOk);
		}
		FSeam_NetValue Out;
		{
			FMemoryReader Reader(Bytes, /*bIsPersistent*/ true);
			bool bOk = false;
			Out.NetSerialize(Reader, nullptr, bOk);
			TestTrue(FString::Printf(TEXT("%s read ok"), Label), bOk);
		}
		TestTrue(FString::Printf(TEXT("%s survives roundtrip"), Label), In == Out);
	};

	Roundtrip(FSeam_NetValue(), TEXT("none"));
	Roundtrip(FSeam_NetValue::MakeBool(true), TEXT("bool"));
	Roundtrip(FSeam_NetValue::MakeInt(-12345), TEXT("int"));
	Roundtrip(FSeam_NetValue::MakeFloat(2.71828), TEXT("float"));
	Roundtrip(FSeam_NetValue::MakeVector(FVector(10, -20, 30)), TEXT("vector"));
	Roundtrip(FSeam_NetValue::MakeName(FName(TEXT("TestName"))), TEXT("name"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDPNetValueInstancedStructConvTest,
	"DesignPatterns.Seams.NetValue.InstancedStructConversion",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDPNetValueInstancedStructConvTest::RunTest(const FString& /*Parameters*/)
{
	// A vector value converts to an FInstancedStruct and back without loss.
	const FSeam_NetValue V = FSeam_NetValue::MakeVector(FVector(4, 5, 6));
	FInstancedStruct AsStruct;
	TestTrue(TEXT("vector -> instanced struct"), V.ToInstancedStruct(AsStruct));
	TestTrue(TEXT("instanced struct is valid"), AsStruct.IsValid());

	bool bOk = false;
	const FSeam_NetValue Back = FSeam_NetValue::FromInstancedStruct(AsStruct, bOk);
	TestTrue(TEXT("instanced struct -> vector ok"), bOk);
	TestTrue(TEXT("vector survives struct conversion"), Back == V);

	// A None value reports failure to convert (nothing to wrap).
	FInstancedStruct Empty;
	TestFalse(TEXT("none does not convert"), FSeam_NetValue().ToInstancedStruct(Empty));

	return true;
}

#endif // WITH_AUTOMATION_TESTS
