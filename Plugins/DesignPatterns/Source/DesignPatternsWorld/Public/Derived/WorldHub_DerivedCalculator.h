// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"

// FInstancedStruct lives in StructUtils on UE 5.3/5.4, merged into CoreUObject in 5.5.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "WorldHub_DerivedCalculator.generated.h"

/**
 * Strategy base for ONE pure, side-effect-free computed world-hub value.
 *
 * Given the resolved dependency values, Compute returns the derived FInstancedStruct. The OWNING
 * component (not the calculator) performs the authoritative write back into the hub — the calculator
 * is intentionally pure so it can be unit-reasoned and so it never writes state or reads authority.
 *
 * Data-instanced (EditInlineNew, Blueprintable) so games author new formulas with no C++ change; all
 * tunables live as EditAnywhere UPROPERTYs on the subclass / data asset, never as hard-coded numbers.
 */
UCLASS(Abstract, EditInlineNew, Blueprintable, DefaultToInstanced)
class DESIGNPATTERNSWORLD_API UWorldHub_DerivedCalculator : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Compute the derived value from its dependencies.
	 * @param Deps Map of dependency key -> currently-resolved value (effective value with fallback).
	 * @param Out  The computed derived value (set on success).
	 * @return true if a value was produced (false leaves the derived slot untouched).
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|WorldHub|Derived")
	bool Compute(const TMap<FGameplayTag, FInstancedStruct>& Deps, FInstancedStruct& Out) const;
	virtual bool Compute_Implementation(const TMap<FGameplayTag, FInstancedStruct>& Deps, FInstancedStruct& Out) const;

	/**
	 * Declare which hub keys this calculator depends on. The component unions these with the binding's
	 * authored DependsOn list to build the reverse dependency index. Default: none (override or author).
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|WorldHub|Derived")
	void CollectDependencies(TArray<FGameplayTag>& OutDeps) const;
	virtual void CollectDependencies_Implementation(TArray<FGameplayTag>& OutDeps) const;
};

/**
 * A built-in calculator that sums the integer values of all its dependencies (a common derived case:
 * total resources / total reputation). Demonstrates the pure-Strategy contract; games subclass for
 * bespoke formulas. No magic numbers — the dependency set is authored on the binding.
 */
UCLASS(meta = (DisplayName = "WorldHub Derived: Sum Of Int Dependencies"))
class DESIGNPATTERNSWORLD_API UWorldHub_DerivedCalculator_SumInt : public UWorldHub_DerivedCalculator
{
	GENERATED_BODY()

public:
	virtual bool Compute_Implementation(const TMap<FGameplayTag, FInstancedStruct>& Deps, FInstancedStruct& Out) const override;
};
