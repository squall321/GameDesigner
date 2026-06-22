// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "Hub/WorldHub_Scope.h"
#include "WorldHub_DerivedValueSetDataAsset.generated.h"

class UWorldHub_DerivedCalculator;

/**
 * The authored definition of one derived/computed hub value.
 *
 * OutKey is recomputed (via Calculator) whenever any key in DependsOn changes, and the result is
 * written back through the hub's authoritative SetVariable so the derived key replicates and persists
 * EXACTLY like a stored slot — but only if a matching flag definition with bReplicate/bSave exists for
 * OutKey (authored in a UWorldHub_FlagSetDataAsset); otherwise the derived value stays server-local by
 * design. No magic numbers live here: tunables belong on the Calculator instance.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSWORLD_API FWorldHub_DerivedDefinition
{
	GENERATED_BODY()

	/** The key the computed result is written under. Must be unique within the set. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|WorldHub|Derived")
	FGameplayTag OutKey;

	/** The scope the result is written into (and the scope dependencies are read from). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|WorldHub|Derived")
	FWorldHub_Scope Scope;

	/** Keys whose change triggers a recompute of OutKey (unioned with Calculator->CollectDependencies). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|WorldHub|Derived")
	TArray<FGameplayTag> DependsOn;

	/** The pure Strategy that computes OutKey from its dependency values. */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "DesignPatterns|WorldHub|Derived")
	TObjectPtr<UWorldHub_DerivedCalculator> Calculator;

	FWorldHub_DerivedDefinition() = default;
};

/**
 * A reusable set of derived-value definitions, authored once and applied by a
 * UWorldHub_DerivedValueComponent at BeginPlay. Subclass of UDP_DataAsset so it is tag-addressable.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSWORLD_API UWorldHub_DerivedValueSetDataAsset : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/** Every derived value this set defines. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|WorldHub|Derived")
	TArray<FWorldHub_DerivedDefinition> Definitions;
};
