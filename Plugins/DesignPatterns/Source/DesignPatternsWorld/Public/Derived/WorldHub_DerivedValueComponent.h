// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UObject/WeakInterfacePtr.h"
#include "GameplayTagContainer.h"
#include "Hub/WorldHub_Scope.h"
#include "Net/Seam_NetValue.h"
#include "WorldHub_DerivedValueComponent.generated.h"

class UWorldHub_StateHubSubsystem;
class UWorldHub_DerivedCalculator;
class UWorldHub_DerivedValueSetDataAsset;

/**
 * One runtime derived-value binding (resolved from the data asset at register time).
 *
 * Not replicated: the COMPONENT computes the derived value on the authority and writes it back into
 * the hub, which replicates the resulting slot through the existing rep carrier. The binding itself is
 * authority-side bookkeeping.
 */
USTRUCT()
struct DESIGNPATTERNSWORLD_API FWorldHub_DerivedBinding
{
	GENERATED_BODY()

	/** The key the computed result is written under. */
	UPROPERTY()
	FGameplayTag OutKey;

	/** The scope the result is written into / dependencies are read from. */
	UPROPERTY()
	FWorldHub_Scope Scope;

	/** Keys whose change triggers a recompute (authored + calculator-declared). */
	UPROPERTY()
	TArray<FGameplayTag> DependsOn;

	/** The pure Strategy that computes the value. */
	UPROPERTY()
	TObjectPtr<UWorldHub_DerivedCalculator> Calculator = nullptr;

	FWorldHub_DerivedBinding() = default;
};

/**
 * DERIVED / computed hub values, AUTHORITY-side.
 *
 * Placed on the SAME always-relevant carrier actor as UWorldHub_StateRepComponent. Owns registered
 * computed keys with declared dependencies; when a dependency changes it recomputes (via a pure
 * UWorldHub_DerivedCalculator Strategy) and writes the result back through the hub's authoritative
 * SetVariable — so a derived key replicates and saves like any slot (provided OutKey has a
 * replicable/save flag definition; otherwise it stays server-local by design).
 *
 * OnHubValueChanged early-returns unless Hub->HasWorldAuthority(). Recompute is re-entrancy-safe: a
 * dirty-set + topological single-pass with an in-progress guard and self-write suppression, and the
 * registrar rejects cycles at register time.
 */
UCLASS(ClassGroup = (DesignPatternsWorld), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSWORLD_API UWorldHub_DerivedValueComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UWorldHub_DerivedValueComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent

	/** Optional authored set applied at BeginPlay (AUTHORITY side). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|WorldHub|Derived")
	TObjectPtr<UWorldHub_DerivedValueSetDataAsset> DerivedSet;

	/**
	 * Register a derived binding (AUTHORITY ONLY). Rejects a binding whose dependency graph would form a
	 * cycle with the already-registered bindings. @return true if registered.
	 */
	bool RegisterDerived(const FWorldHub_DerivedBinding& Binding);

	/** Recompute a single derived key now (pulls deps via the hub, writes the result via the hub). */
	void RecomputeKey(const FGameplayTag& OutKey);

	/** Recompute every registered derived key (e.g. after a load / seed). AUTHORITY ONLY. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|WorldHub|Derived")
	void RecomputeAll();

private:
	/** Authority check delegated to the hub (the single source of truth for world authority). */
	bool HasAuthority() const;

	/** Bound to the hub's OnValueChanged: dirties dependents of the changed key and recomputes them. */
	UFUNCTION()
	void OnHubValueChanged(FWorldHub_Scope Scope, FGameplayTag Key, FSeam_NetValue NewValue);

	/** Resolve / cache the world hub and bind its OnValueChanged. */
	UWorldHub_StateHubSubsystem* ResolveHub();

	/** Apply the authored DerivedSet (AUTHORITY ONLY). */
	void ApplyDerivedSet();

	/** Build the reverse dependency index (dependency key -> dependent OutKeys) from Bindings. */
	void RebuildReverseIndex();

	/** Returns true if adding (OutKey depends on DependsOn) would introduce a cycle. */
	bool WouldFormCycle(const FGameplayTag& OutKey, const TArray<FGameplayTag>& DependsOn) const;

	/** All registered bindings (AUTHORITY-side bookkeeping). */
	UPROPERTY()
	TArray<FWorldHub_DerivedBinding> Bindings;

	/** dependency key -> indices into Bindings that depend on it. Rebuilt on register. */
	TMap<FGameplayTag, TArray<int32>> DependentsByDep;

	/** OutKey -> index into Bindings, for fast lookup of the binding to recompute. */
	TMap<FGameplayTag, int32> BindingByOutKey;

	/** The hub this drives (re-resolved lazily; never owned). */
	TWeakObjectPtr<UWorldHub_StateHubSubsystem> Hub;

	/** Self-write suppression / re-entrancy guard while a recompute pass is in progress. */
	bool bRecomputing = false;
};
