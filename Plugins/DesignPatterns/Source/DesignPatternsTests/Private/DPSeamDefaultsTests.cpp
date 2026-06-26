// Copyright DesignPatterns plugin. All Rights Reserved.

#if WITH_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

// Seam headers under test. Each declares a BlueprintNativeEvent UINTERFACE whose inert-default
// _Implementation bodies were once MISSING (a latent unresolved-external link error). These tests
// regression-guard that class of bug: a tiny test UObject implements each seam and overrides NOTHING,
// then we call the generated Execute_<Method> wrapper. The call only links + runs if the inert default
// body exists, so a reverted default fails here; we also assert the documented fail-safe value.
#include "Combat/Seam_DamageReactor.h"
#include "Persist/Seam_Persistable.h"
#include "Economy/Seam_ResourceConsumer.h"
#include "Economy/Seam_ResourceProducer.h"
#include "Display/Seam_SafeZoneProvider.h"
#include "Loc/Seam_AccessibilityConsumer.h"
#include "Loc/Seam_AccessibilityTypes.h"

#include "DPSeamDefaultsTests.generated.h"

// ---------------------------------------------------------------------------------------------------
// Throwaway test implementers. Each implements ONE seam UINTERFACE and overrides nothing, so every
// Execute_ call resolves to the seam's own inert default _Implementation.
// ---------------------------------------------------------------------------------------------------

UCLASS(Transient, NotBlueprintable)
class UDPTest_DamageReactorImpl : public UObject, public ISeam_DamageReactor
{
	GENERATED_BODY()
};

UCLASS(Transient, NotBlueprintable)
class UDPTest_PersistableImpl : public UObject, public ISeam_Persistable
{
	GENERATED_BODY()
};

UCLASS(Transient, NotBlueprintable)
class UDPTest_ResourceConsumerImpl : public UObject, public ISeam_ResourceConsumer
{
	GENERATED_BODY()
};

UCLASS(Transient, NotBlueprintable)
class UDPTest_ResourceProducerImpl : public UObject, public ISeam_ResourceProducer
{
	GENERATED_BODY()
};

UCLASS(Transient, NotBlueprintable)
class UDPTest_SafeZoneProviderImpl : public UObject, public ISeam_SafeZoneProvider
{
	GENERATED_BODY()
};

UCLASS(Transient, NotBlueprintable)
class UDPTest_AccessibilityConsumerImpl : public UObject, public ISeam_AccessibilityConsumer
{
	GENERATED_BODY()
};

// ---------------------------------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDPSeamDamageReactorDefaultTest,
	"DesignPatterns.Seams.Defaults.DamageReactor",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDPSeamDamageReactorDefaultTest::RunTest(const FString& /*Parameters*/)
{
	UDPTest_DamageReactorImpl* Obj = NewObject<UDPTest_DamageReactorImpl>(GetTransientPackage());
	TestNotNull(TEXT("reactor impl constructed"), Obj);

	// The inert defaults are no-ops; the regression guard is simply that these Execute_ calls LINK and
	// run without crashing (they would be unresolved externals if the default .cpp bodies were missing).
	ISeam_DamageReactor::Execute_OnDamageResolved(Obj, nullptr, 10.f, FGameplayTag(), FGameplayTag());
	ISeam_DamageReactor::Execute_OnDefeated(Obj, nullptr);

	TestTrue(TEXT("damage-reactor defaults are callable no-ops"), true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDPSeamPersistableDefaultTest,
	"DesignPatterns.Seams.Defaults.Persistable",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDPSeamPersistableDefaultTest::RunTest(const FString& /*Parameters*/)
{
	UDPTest_PersistableImpl* Obj = NewObject<UDPTest_PersistableImpl>(GetTransientPackage());
	TestNotNull(TEXT("persistable impl constructed"), Obj);

	// GetPersistenceKind default is an invalid tag (nothing to route).
	const FGameplayTag Kind = ISeam_Persistable::Execute_GetPersistenceKind(Obj);
	TestFalse(TEXT("default persistence kind is invalid"), Kind.IsValid());

	// Capture/Restore are no-ops by default; just assert they are callable (link guard).
	FInstancedStruct State;
	ISeam_Persistable::Execute_CaptureState(Obj, State);
	ISeam_Persistable::Execute_RestoreState(Obj, State);
	TestTrue(TEXT("capture/restore defaults are callable no-ops"), true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDPSeamResourceConsumerDefaultTest,
	"DesignPatterns.Seams.Defaults.ResourceConsumer",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDPSeamResourceConsumerDefaultTest::RunTest(const FString& /*Parameters*/)
{
	UDPTest_ResourceConsumerImpl* Obj = NewObject<UDPTest_ResourceConsumerImpl>(GetTransientPackage());
	TestNotNull(TEXT("consumer impl constructed"), Obj);

	// Fail-closed defaults: idle, not consuming, not starved, no progress, rejects process changes.
	TestFalse(TEXT("default not consuming"), ISeam_ResourceConsumer::Execute_IsConsuming(Obj));
	TestFalse(TEXT("default not starved"), ISeam_ResourceConsumer::Execute_IsStarved(Obj));
	TestEqual(TEXT("default progress is 0"), ISeam_ResourceConsumer::Execute_GetConsumptionProgress(Obj), 0.f);
	TestFalse(TEXT("default active process tag invalid"), ISeam_ResourceConsumer::Execute_GetActiveProcessTag(Obj).IsValid());
	TestFalse(TEXT("default rejects SetActiveProcess"), ISeam_ResourceConsumer::Execute_SetActiveProcess(Obj, FGameplayTag()));

	TArray<FGameplayTag> Commodities;
	TArray<float> Quantities;
	ISeam_ResourceConsumer::Execute_GetExpectedInputs(Obj, Commodities, Quantities);
	TestEqual(TEXT("default expected-input commodities empty"), Commodities.Num(), 0);
	TestEqual(TEXT("default expected-input quantities empty"), Quantities.Num(), 0);

	// CancelConsumption is a callable no-op.
	ISeam_ResourceConsumer::Execute_CancelConsumption(Obj);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDPSeamResourceProducerDefaultTest,
	"DesignPatterns.Seams.Defaults.ResourceProducer",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDPSeamResourceProducerDefaultTest::RunTest(const FString& /*Parameters*/)
{
	UDPTest_ResourceProducerImpl* Obj = NewObject<UDPTest_ResourceProducerImpl>(GetTransientPackage());
	TestNotNull(TEXT("producer impl constructed"), Obj);

	TestFalse(TEXT("default not producing"), ISeam_ResourceProducer::Execute_IsProducing(Obj));
	TestEqual(TEXT("default progress is 0"), ISeam_ResourceProducer::Execute_GetProductionProgress(Obj), 0.f);
	TestFalse(TEXT("default active process tag invalid"), ISeam_ResourceProducer::Execute_GetActiveProcessTag(Obj).IsValid());
	TestFalse(TEXT("default rejects SetActiveProcess"), ISeam_ResourceProducer::Execute_SetActiveProcess(Obj, FGameplayTag()));

	TArray<FGameplayTag> Commodities;
	TArray<float> Quantities;
	ISeam_ResourceProducer::Execute_GetExpectedOutputs(Obj, Commodities, Quantities);
	TestEqual(TEXT("default expected-output commodities empty"), Commodities.Num(), 0);
	TestEqual(TEXT("default expected-output quantities empty"), Quantities.Num(), 0);

	ISeam_ResourceProducer::Execute_CancelProduction(Obj);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDPSeamSafeZoneProviderDefaultTest,
	"DesignPatterns.Seams.Defaults.SafeZoneProvider",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDPSeamSafeZoneProviderDefaultTest::RunTest(const FString& /*Parameters*/)
{
	UDPTest_SafeZoneProviderImpl* Obj = NewObject<UDPTest_SafeZoneProviderImpl>(GetTransientPackage());
	TestNotNull(TEXT("safe-zone impl constructed"), Obj);

	// Default: no safe-zone -> zero insets, unscaled DPI, zero resolution.
	const FVector4 Insets = ISeam_SafeZoneProvider::Execute_GetSafeInsets(Obj);
	TestTrue(TEXT("default insets are zero"), Insets == FVector4(0, 0, 0, 0));
	TestEqual(TEXT("default DPI scale is 1"), ISeam_SafeZoneProvider::Execute_GetDPIScale(Obj), 1.f);
	TestTrue(TEXT("default resolution is zero"), ISeam_SafeZoneProvider::Execute_GetResolution(Obj) == FIntPoint::ZeroValue);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDPSeamAccessibilityConsumerDefaultTest,
	"DesignPatterns.Seams.Defaults.AccessibilityConsumer",
	EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDPSeamAccessibilityConsumerDefaultTest::RunTest(const FString& /*Parameters*/)
{
	UDPTest_AccessibilityConsumerImpl* Obj = NewObject<UDPTest_AccessibilityConsumerImpl>(GetTransientPackage());
	TestNotNull(TEXT("accessibility consumer impl constructed"), Obj);

	// Default is a no-op; the guard is that the Execute_ call links and runs without crashing.
	const FSeam_AccessibilityOptions Options;
	ISeam_AccessibilityConsumer::Execute_OnAccessibilityOptionsChanged(Obj, Options);
	TestTrue(TEXT("accessibility default is a callable no-op"), true);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
