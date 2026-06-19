// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Strategy/DPStrategy.h"
#include "DPStateMachineComponent.generated.h"

class UDP_StateMachineDefinition;
class UDP_State;
class UDP_Blackboard;

/**
 * Fired after the active state changes (locally or via OnRep). From may be invalid on first entry.
 * @param From the tag of the state just left (invalid if none).
 * @param To   the tag of the newly active state.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDP_OnStateChanged, FGameplayTag, From, FGameplayTag, To);

/**
 * The RUNTIME half of the FSM: a lightweight per-actor component that runs a SHARED definition.
 *
 * Central critique fix in practice — this component holds NO copy of the state graph. It points at
 * a shared UDP_StateMachineDefinition (authored once, reused everywhere) and owns only the small
 * per-instance runtime: a UDP_Blackboard and the active state tag. Only ActiveStateTag replicates
 * (states/guards/strategies are deterministic data already present on every client via the asset),
 * so network cost is a single tag. Ticks the active state, then evaluates its transitions by
 * descending priority and takes the first eligible edge.
 */
UCLASS(ClassGroup = (DesignPatterns), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNS_API UDP_StateMachineComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDP_StateMachineComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent

	/**
	 * Transition to the state with the given tag.
	 * Runs the current state's OnExit, the target's OnEnter, fires OnStateChanged and (on authority)
	 * replicates the new tag.
	 * @param NewStateTag destination state's tag (must exist in the definition).
	 * @param bForce when true, skip the target's CanEnter / inbound-guard admission check.
	 * @return true if the state actually changed.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|FSM")
	bool ChangeState(FGameplayTag NewStateTag, bool bForce = false);

	/** @return the currently active state object (resolved from the shared definition), or null. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|FSM")
	UDP_State* GetActiveState() const;

	/** @return the tag of the currently active state. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|FSM")
	FGameplayTag GetActiveStateTag() const { return ActiveStateTag; }

	/** @return the per-instance blackboard, creating it lazily if needed. Never null after BeginPlay. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|FSM")
	UDP_Blackboard* GetBlackboard() const { return Blackboard; }

	/** (Re)start the machine at the definition's InitialStateTag. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|FSM")
	void RestartToInitialState();

	/** Build a strategy context (owner + blackboard provider) for this component's current state. */
	FDP_StrategyContext MakeStrategyContext() const;

	/** One-line status string for the gameplay debugger / DP.FSM.LogState command. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|FSM")
	FString GetDebugString() const;

	/** Broadcast after every successful state change. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|FSM")
	FDP_OnStateChanged OnStateChanged;

	/** Shared, authored-once state graph. Multiple components may point at the same asset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|FSM")
	TObjectPtr<UDP_StateMachineDefinition> Definition;

	/** When true, transitions are evaluated every tick. Disable for purely event-driven FSMs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|FSM")
	bool bEvaluateTransitionsOnTick = true;

protected:
	/** Evaluate the active state's transitions by descending priority; take the first eligible. */
	void EvaluateTransitions();

	/** Replicated-only runtime: the active state's tag. Everything else derives from the asset. */
	UPROPERTY(ReplicatedUsing = OnRep_ActiveStateTag, VisibleInstanceOnly, Category = "DesignPatterns|FSM")
	FGameplayTag ActiveStateTag;

	/** Client-side reaction to a replicated state change: run exit/enter hooks + fire the delegate. */
	UFUNCTION()
	void OnRep_ActiveStateTag(FGameplayTag PreviousStateTag);

	/** Per-instance typed key/value store. Owned subobject (outer = this), GC-visible. */
	UPROPERTY(VisibleInstanceOnly, Instanced, Category = "DesignPatterns|FSM")
	TObjectPtr<UDP_Blackboard> Blackboard;

private:
	/**
	 * Shared transition logic for both authoritative ChangeState and client OnRep. Resolves states,
	 * runs OnExit(Previous)/OnEnter(New), VLogs, and fires OnStateChanged. Does NOT touch replication.
	 */
	void ApplyStateChange(FGameplayTag PreviousTag, FGameplayTag NewTag);

	/** @return true if NewState admits entry (CanEnter passes), or bForce. */
	bool PassesEntryAdmission(UDP_State* NewState, bool bForce) const;
};
