// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Derived/WorldHub_DerivedCalculator.h"
#include "Net/Seam_NetValue.h"

bool UWorldHub_DerivedCalculator::Compute_Implementation(const TMap<FGameplayTag, FInstancedStruct>& /*Deps*/, FInstancedStruct& /*Out*/) const
{
	// Base does nothing; subclasses override with a concrete pure formula.
	return false;
}

void UWorldHub_DerivedCalculator::CollectDependencies_Implementation(TArray<FGameplayTag>& OutDeps) const
{
	// Base declares no implicit dependencies; the binding's authored DependsOn list drives it.
	OutDeps.Reset();
}

bool UWorldHub_DerivedCalculator_SumInt::Compute_Implementation(const TMap<FGameplayTag, FInstancedStruct>& Deps, FInstancedStruct& Out) const
{
	int64 Sum = 0;
	for (const TPair<FGameplayTag, FInstancedStruct>& Pair : Deps)
	{
		const FInstancedStruct& Value = Pair.Value;
		if (!Value.IsValid())
		{
			continue;
		}
		// Use the net-value projection to read the int losslessly regardless of inner storage.
		bool bOk = false;
		const FSeam_NetValue Net = FSeam_NetValue::FromInstancedStruct(Value, bOk);
		if (bOk && Net.Type == ESeam_NetValueType::Int)
		{
			Sum += Net.IntValue;
		}
	}
	Out.InitializeAs<int64>(Sum);
	return true;
}
