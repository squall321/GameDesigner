// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Derived/WorldHub_DerivedValueComponent.h"
#include "Derived/WorldHub_DerivedCalculator.h"
#include "Derived/WorldHub_DerivedValueSetDataAsset.h"
#include "Hub/WorldHub_StateHubSubsystem.h"

#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"

UWorldHub_DerivedValueComponent::UWorldHub_DerivedValueComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UWorldHub_DerivedValueComponent::BeginPlay()
{
	Super::BeginPlay();
	ResolveHub();

	// Seeding derived values is an authoritative write.
	if (HasAuthority())
	{
		ApplyDerivedSet();
		RecomputeAll();
	}
}

void UWorldHub_DerivedValueComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorldHub_StateHubSubsystem* H = Hub.Get())
	{
		H->OnValueChanged.RemoveAll(this);
	}
	Bindings.Reset();
	DependentsByDep.Reset();
	BindingByOutKey.Reset();
	Hub.Reset();

	Super::EndPlay(EndPlayReason);
}

bool UWorldHub_DerivedValueComponent::HasAuthority() const
{
	const UWorldHub_StateHubSubsystem* H = Hub.Get();
	return H && H->HasWorldAuthority();
}

UWorldHub_StateHubSubsystem* UWorldHub_DerivedValueComponent::ResolveHub()
{
	if (UWorldHub_StateHubSubsystem* Cached = Hub.Get())
	{
		return Cached;
	}
	UWorldHub_StateHubSubsystem* Resolved =
		FDP_SubsystemStatics::GetWorldSubsystem<UWorldHub_StateHubSubsystem>(this);
	if (Resolved)
	{
		Hub = Resolved;
		Resolved->OnValueChanged.AddUniqueDynamic(this, &UWorldHub_DerivedValueComponent::OnHubValueChanged);
	}
	return Resolved;
}

void UWorldHub_DerivedValueComponent::ApplyDerivedSet()
{
	if (!DerivedSet)
	{
		return;
	}
	for (const FWorldHub_DerivedDefinition& Def : DerivedSet->Definitions)
	{
		if (!Def.OutKey.IsValid() || !Def.Calculator)
		{
			UE_LOG(LogDP, Warning, TEXT("[WorldHub] Derived definition skipped (invalid key or null calculator)."));
			continue;
		}
		FWorldHub_DerivedBinding Binding;
		Binding.OutKey = Def.OutKey;
		Binding.Scope = Def.Scope;
		Binding.Calculator = Def.Calculator;

		// Union authored DependsOn with the calculator's declared dependencies.
		Binding.DependsOn = Def.DependsOn;
		TArray<FGameplayTag> CalcDeps;
		Def.Calculator->CollectDependencies(CalcDeps);
		for (const FGameplayTag& Dep : CalcDeps)
		{
			Binding.DependsOn.AddUnique(Dep);
		}

		RegisterDerived(Binding);
	}
}

bool UWorldHub_DerivedValueComponent::RegisterDerived(const FWorldHub_DerivedBinding& Binding)
{
	// AUTHORITY GUARD at the TOP.
	if (!HasAuthority())
	{
		return false;
	}
	if (!Binding.OutKey.IsValid() || !Binding.Calculator)
	{
		return false;
	}
	if (BindingByOutKey.Contains(Binding.OutKey))
	{
		UE_LOG(LogDP, Warning, TEXT("[WorldHub] Derived OutKey %s already registered; ignoring duplicate."),
			*Binding.OutKey.ToString());
		return false;
	}
	if (WouldFormCycle(Binding.OutKey, Binding.DependsOn))
	{
		UE_LOG(LogDP, Error, TEXT("[WorldHub] Derived OutKey %s rejected: dependency cycle detected."),
			*Binding.OutKey.ToString());
		return false;
	}

	const int32 Index = Bindings.Add(Binding);
	BindingByOutKey.Add(Binding.OutKey, Index);
	RebuildReverseIndex();
	return true;
}

void UWorldHub_DerivedValueComponent::RebuildReverseIndex()
{
	DependentsByDep.Reset();
	for (int32 i = 0; i < Bindings.Num(); ++i)
	{
		for (const FGameplayTag& Dep : Bindings[i].DependsOn)
		{
			if (Dep.IsValid())
			{
				DependentsByDep.FindOrAdd(Dep).AddUnique(i);
			}
		}
	}
}

bool UWorldHub_DerivedValueComponent::WouldFormCycle(const FGameplayTag& OutKey, const TArray<FGameplayTag>& DependsOn) const
{
	// A cycle exists if OutKey is (transitively) reachable from any of its own dependencies through the
	// existing OutKey -> DependsOn edges. DFS from each dependency.
	TArray<FGameplayTag> Stack = DependsOn;
	TSet<FGameplayTag> Visited;

	while (Stack.Num() > 0)
	{
		const FGameplayTag Current = Stack.Pop(/*bAllowShrinking=*/false);
		if (Current == OutKey)
		{
			return true; // Reached ourselves -> cycle.
		}
		if (Visited.Contains(Current))
		{
			continue;
		}
		Visited.Add(Current);

		// If Current is itself a derived OutKey, push its dependencies.
		if (const int32* FoundIndex = BindingByOutKey.Find(Current))
		{
			if (Bindings.IsValidIndex(*FoundIndex))
			{
				Stack.Append(Bindings[*FoundIndex].DependsOn);
			}
		}
	}
	return false;
}

void UWorldHub_DerivedValueComponent::OnHubValueChanged(FWorldHub_Scope /*Scope*/, FGameplayTag Key, FSeam_NetValue /*NewValue*/)
{
	// AUTHORITY GUARD at the TOP.
	if (!HasAuthority())
	{
		return;
	}
	// Suppress recursion from our own writes (a recompute's SetVariable re-enters here).
	if (bRecomputing)
	{
		return;
	}

	const TArray<int32>* Dependents = DependentsByDep.Find(Key);
	if (!Dependents)
	{
		return;
	}

	// Topological single-pass: recompute affected OutKeys; bRecomputing absorbs the cascade so each
	// dependent's own write does not trigger a recursive dispatch (the guard early-returns above).
	TGuardValue<bool> RecomputeGuard(bRecomputing, true);

	// Process a work queue so a derived key that feeds another derived key recomputes in order.
	TArray<int32> Work = *Dependents;
	TSet<int32> Processed;
	for (int32 Cursor = 0; Cursor < Work.Num(); ++Cursor)
	{
		const int32 BindingIndex = Work[Cursor];
		if (Processed.Contains(BindingIndex) || !Bindings.IsValidIndex(BindingIndex))
		{
			continue;
		}
		Processed.Add(BindingIndex);

		const FGameplayTag OutKey = Bindings[BindingIndex].OutKey;
		RecomputeKey(OutKey);

		// Cascade: anything depending on this OutKey must also recompute.
		if (const TArray<int32>* Downstream = DependentsByDep.Find(OutKey))
		{
			Work.Append(*Downstream);
		}
	}
}

void UWorldHub_DerivedValueComponent::RecomputeKey(const FGameplayTag& OutKey)
{
	UWorldHub_StateHubSubsystem* H = ResolveHub();
	if (!H || !HasAuthority())
	{
		return;
	}
	const int32* FoundIndex = BindingByOutKey.Find(OutKey);
	if (!FoundIndex || !Bindings.IsValidIndex(*FoundIndex))
	{
		return;
	}
	const FWorldHub_DerivedBinding& Binding = Bindings[*FoundIndex];
	if (!Binding.Calculator)
	{
		return;
	}

	// Resolve dependency values (effective values, with the hub's scope fallback).
	TMap<FGameplayTag, FInstancedStruct> Deps;
	Deps.Reserve(Binding.DependsOn.Num());
	for (const FGameplayTag& Dep : Binding.DependsOn)
	{
		FInstancedStruct Value;
		if (H->QueryValue(Dep, Binding.Scope, Value))
		{
			Deps.Add(Dep, Value);
		}
	}

	FInstancedStruct Result;
	if (Binding.Calculator->Compute(Deps, Result) && Result.IsValid())
	{
		// Write back through the authoritative path; replicates/saves per OutKey's flag definition.
		H->SetVariable(OutKey, Result, Binding.Scope);
	}
}

void UWorldHub_DerivedValueComponent::RecomputeAll()
{
	if (!HasAuthority())
	{
		return;
	}
	TGuardValue<bool> RecomputeGuard(bRecomputing, true);
	for (const FWorldHub_DerivedBinding& Binding : Bindings)
	{
		RecomputeKey(Binding.OutKey);
	}
}
