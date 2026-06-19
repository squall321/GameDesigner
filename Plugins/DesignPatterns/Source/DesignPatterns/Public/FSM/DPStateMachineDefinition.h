// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "DPStateMachineDefinition.generated.h"

class UDP_State;

/**
 * Shared, authored-once state-graph asset (the DEFINITION half of the FSM).
 *
 * This is the central critique fix: the graph (states, transitions, guards, strategies) is data
 * that lives on ONE UPrimaryDataAsset and is referenced by every UDP_StateMachineComponent using
 * it. Components hold no copy of the graph — only a lightweight pointer plus their per-instance
 * blackboard and active tag. Because states are stateless and shared, this asset is fully
 * diff-friendly and reusable across thousands of instances with no per-instance graph allocation.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNS_API UDP_StateMachineDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Asset-registry id; groups all FSM definitions under one primary asset type. */
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

	/** The state graph. Instanced so each UDP_State subobject is owned by (and saved with) this asset. */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "DesignPatterns|FSM")
	TArray<TObjectPtr<UDP_State>> States;

	/** Tag of the state instances start in. Must match one entry's StateTag. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|FSM")
	FGameplayTag InitialStateTag;

	/** @return the state object whose StateTag == Tag, or null if none. O(n) over a small graph. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|FSM")
	UDP_State* FindState(FGameplayTag Tag) const;

	/** @return true if a state with this tag exists in the graph. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|FSM")
	bool HasState(FGameplayTag Tag) const;

	/** @return the resolved initial state, or null when InitialStateTag is unset/invalid. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|FSM")
	UDP_State* GetInitialState() const;

#if WITH_EDITOR
	/** Editor validation: flags duplicate/empty state tags, dangling transitions, missing initial. */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
