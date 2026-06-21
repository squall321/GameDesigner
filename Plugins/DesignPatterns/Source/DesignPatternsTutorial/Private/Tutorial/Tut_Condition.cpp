// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Tutorial/Tut_Condition.h"

#include "Core/DPLog.h"

#include "Net/Seam_NetValue.h"

// FInstancedStruct lives in StructUtils on 5.3/5.4 and in CoreUObject on 5.5+ (value decoding below).
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

// -------------------------------------------------------------------------------------------------
// UTut_Condition (base)
// -------------------------------------------------------------------------------------------------

const ITut_ConditionContext* UTut_Condition::GetContext(const UObject* WorldContext)
{
	// Cast<> works for a native C++ interface; returns null cleanly when the object does not implement it.
	return Cast<ITut_ConditionContext>(WorldContext);
}

bool UTut_Condition::Evaluate_Implementation(UObject* /*WorldContext*/) const
{
	// The abstract base satisfies nothing; concrete subclasses (or BP overrides) provide the real test.
	return false;
}

FString UTut_Condition::DescribeCondition_Implementation() const
{
	return GetClass() ? GetClass()->GetName() : TEXT("<null condition>");
}

// -------------------------------------------------------------------------------------------------
// UTut_Condition_BusEvent
// -------------------------------------------------------------------------------------------------

bool UTut_Condition_BusEvent::Evaluate_Implementation(UObject* WorldContext) const
{
	if (!EventTag.IsValid())
	{
		return false;
	}

	const ITut_ConditionContext* Context = GetContext(WorldContext);
	if (!Context)
	{
		return false;
	}

	return Context->HasSeenBusEvent(EventTag);
}

FString UTut_Condition_BusEvent::DescribeCondition_Implementation() const
{
	return FString::Printf(TEXT("BusEventSeen(%s)"), *EventTag.ToString());
}

// -------------------------------------------------------------------------------------------------
// UTut_Condition_HubFlag
// -------------------------------------------------------------------------------------------------

bool UTut_Condition_HubFlag::Evaluate_Implementation(UObject* WorldContext) const
{
	if (!FlagKey.IsValid())
	{
		return false;
	}

	const ITut_ConditionContext* Context = GetContext(WorldContext);
	if (!Context)
	{
		return false;
	}

	FInstancedStruct Value;
	if (!Context->QueryHubValue(FlagKey, Value))
	{
		// No hub, or no value for this key: an unevaluable boolean flag reads as "not the expected value"
		// only when the expectation is true; when the caller expects false, an absent value matches.
		return (bExpected == false);
	}

	bool bOk = false;
	const FSeam_NetValue Net = FSeam_NetValue::FromInstancedStruct(Value, bOk);
	if (!bOk || Net.Type != ESeam_NetValueType::Bool)
	{
		UE_LOG(LogDP, Verbose,
			TEXT("Tut_Condition_HubFlag: hub value for '%s' is not a bool; condition reads false."),
			*FlagKey.ToString());
		return false;
	}

	return Net.bValue == bExpected;
}

FString UTut_Condition_HubFlag::DescribeCondition_Implementation() const
{
	return FString::Printf(TEXT("HubFlag(%s == %s)"),
		*FlagKey.ToString(), bExpected ? TEXT("true") : TEXT("false"));
}

// -------------------------------------------------------------------------------------------------
// UTut_Condition_HubCounterAtLeast
// -------------------------------------------------------------------------------------------------

bool UTut_Condition_HubCounterAtLeast::Evaluate_Implementation(UObject* WorldContext) const
{
	if (!CounterKey.IsValid())
	{
		return false;
	}

	const ITut_ConditionContext* Context = GetContext(WorldContext);
	if (!Context)
	{
		return false;
	}

	FInstancedStruct Value;
	if (!Context->QueryHubValue(CounterKey, Value))
	{
		// Absent counter is treated as zero; satisfied only if the threshold is non-positive.
		return Threshold <= 0;
	}

	bool bOk = false;
	const FSeam_NetValue Net = FSeam_NetValue::FromInstancedStruct(Value, bOk);
	if (!bOk || Net.Type != ESeam_NetValueType::Int)
	{
		UE_LOG(LogDP, Verbose,
			TEXT("Tut_Condition_HubCounterAtLeast: hub value for '%s' is not an int; condition reads false."),
			*CounterKey.ToString());
		return false;
	}

	return Net.IntValue >= Threshold;
}

FString UTut_Condition_HubCounterAtLeast::DescribeCondition_Implementation() const
{
	return FString::Printf(TEXT("HubCounterAtLeast(%s >= %lld)"), *CounterKey.ToString(), Threshold);
}
