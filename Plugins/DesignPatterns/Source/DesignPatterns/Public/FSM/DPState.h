// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "DPState.generated.h"

class UDP_StateMachineComponent;
class UDP_StrategySelector;
class UDP_TransitionGuard;
class UDP_Blackboard;

/**
 * Boolean precondition object guarding a single transition.
 *
 * EditInlineNew so a guard is authored inline on the transition that owns it. Designers (or C++)
 * override EvaluateGuard to test blackboard values, owner state, cooldowns, etc. A null guard on
 * a transition means "always allowed".
 */
UCLASS(Abstract, EditInlineNew, DefaultToInstanced, Blueprintable, CollapseCategories)
class DESIGNPATTERNS_API UDP_TransitionGuard : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * @return true if the guarded transition is permitted right now. Must be side-effect free.
	 * @param OwningComponent the FSM component evaluating the transition (provides world/owner/blackboard).
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "DesignPatterns|FSM")
	bool EvaluateGuard(const UDP_StateMachineComponent* OwningComponent) const;
	virtual bool EvaluateGuard_Implementation(const UDP_StateMachineComponent* OwningComponent) const;
};

/**
 * One authored edge out of a state: where it goes, what gates it, and its evaluation priority.
 * Lives in a UDP_State's transition array inside the shared definition asset.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNS_API FDP_StateTransition
{
	GENERATED_BODY()

	/** Tag of the destination state. Must resolve to a state in the same definition. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|FSM")
	FGameplayTag ToState;

	/** Optional gate. When null the transition is always eligible. Instanced/inline-authored. */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadWrite, Category = "DesignPatterns|FSM")
	TObjectPtr<UDP_TransitionGuard> Guard;

	/** Higher priority transitions are evaluated first; the first eligible one wins. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|FSM")
	int32 Priority = 0;
};

/**
 * A single FSM state: identity tag, outgoing transitions, and lifecycle hooks.
 *
 * EditInlineNew + Blueprintable so states are authored inline inside the shared
 * UDP_StateMachineDefinition and subclassed in C++ or Blueprint. STATELESS by contract: a state
 * object is shared by every instance using the definition, so it must NOT cache per-instance data
 * — all mutable runtime lives on the component's blackboard. Hooks receive the owning component.
 */
UCLASS(Abstract, EditInlineNew, DefaultToInstanced, Blueprintable, CollapseCategories)
class DESIGNPATTERNS_API UDP_State : public UObject
{
	GENERATED_BODY()

public:
	/** Unique identity of this state within its definition (should sit under DP.FSM). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|FSM")
	FGameplayTag StateTag;

	/** Authored outgoing edges, evaluated each tick by descending Priority. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|FSM")
	TArray<FDP_StateTransition> Transitions;

	/**
	 * Optional strategy selector run during OnTick. Lets a state delegate "what to do while here"
	 * to interchangeable, designer-authored strategies (Strategy pattern composed into the FSM).
	 */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadWrite, Category = "DesignPatterns|FSM")
	TObjectPtr<UDP_StrategySelector> StrategySelector;

	/** Called once when this state becomes active. */
	UFUNCTION(BlueprintNativeEvent, Category = "DesignPatterns|FSM")
	void OnEnter(UDP_StateMachineComponent* OwningComponent);
	virtual void OnEnter_Implementation(UDP_StateMachineComponent* OwningComponent);

	/** Called every component tick while active. Default runs StrategySelector if present. */
	UFUNCTION(BlueprintNativeEvent, Category = "DesignPatterns|FSM")
	void OnTick(UDP_StateMachineComponent* OwningComponent, float DeltaSeconds);
	virtual void OnTick_Implementation(UDP_StateMachineComponent* OwningComponent, float DeltaSeconds);

	/** Called once when this state is left. */
	UFUNCTION(BlueprintNativeEvent, Category = "DesignPatterns|FSM")
	void OnExit(UDP_StateMachineComponent* OwningComponent);
	virtual void OnExit_Implementation(UDP_StateMachineComponent* OwningComponent);

	/**
	 * Gate on ENTERING this state (checked in addition to a transition's own guard). Lets a state
	 * declare its own admission rule once instead of repeating it on every inbound transition.
	 * @return true if entry is allowed. Default: always true.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "DesignPatterns|FSM")
	bool CanEnter(UDP_StateMachineComponent* OwningComponent) const;
	virtual bool CanEnter_Implementation(UDP_StateMachineComponent* OwningComponent) const;
};
