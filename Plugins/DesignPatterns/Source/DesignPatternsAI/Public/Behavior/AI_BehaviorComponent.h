// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Identity/Seam_EntityId.h"
#include "Seams/AI_Brain.h"
#include "AI_BehaviorComponent.generated.h"

class UDP_StateMachineComponent;
class UBehaviorTree;
class AAIController;
class UDP_Blackboard;

/**
 * Fired locally after the brain selects a new decision tag (on authority and via OnRep on clients).
 * @param From the previously selected decision tag (invalid on first selection).
 * @param To   the newly selected decision tag (invalid when decisions are disabled).
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FAI_OnDecisionChanged, FGameplayTag, From, FGameplayTag, To);

/**
 * Flat, weak-ref-free bus payload broadcast on DP.Bus.AI.DecisionChanged when a decision is selected.
 * No UObject/weak refs so it is safe to flatten into an FInstancedStruct and defer.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSAI_API FAI_DecisionChangedPayload
{
	GENERATED_BODY()

	/** Stable id of the deciding agent. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatternsAI|Behavior")
	FSeam_EntityId AgentId;

	/** The newly selected decision tag. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatternsAI|Behavior")
	FGameplayTag Decision;

	/** Which backend produced the decision. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatternsAI|Behavior")
	EAI_BrainBackend Backend = EAI_BrainBackend::None;

	FAI_DecisionChangedPayload() = default;
};

/**
 * Behavior BRIDGE: a single component that IMPLEMENTS the IAI_Brain seam over EITHER a core FSM or
 * an engine BehaviorTree, selected by EAI_BrainBackend from data.
 *
 * WRAP, DON'T REINVENT:
 *   - StateMachine backend: drives a core UDP_StateMachineComponent (found on the owner or set
 *     explicitly); RequestDecision evaluates transitions and the active-state tag becomes the
 *     decision. SetTargetEntity writes the target into that component's UDP_Blackboard.
 *   - BehaviorTree backend: runs a UBehaviorTree through the owning pawn's AAIController; the
 *     decision tag is published by tree tasks/services (or mirrored from a blackboard key).
 *
 * REPLICATION: this component carries an AUTHORITATIVE decision tag, so it replicates exactly that
 * one tag (CurrentDecision, ReplicatedUsing) — everything else (FSM graph, BT asset) is deterministic
 * data already present on clients. All IAI_Brain mutators are authority-guarded at the top; clients
 * react to the replicated tag via OnRep and the message bus. Cosmetic systems never write here.
 */
UCLASS(ClassGroup = (DesignPatternsAI), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSAI_API UAI_BehaviorComponent : public UActorComponent, public IAI_Brain
{
	GENERATED_BODY()

public:
	UAI_BehaviorComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent

	//~ Begin IAI_Brain
	virtual void RequestDecision() override;
	virtual void SetDecisionEnabled(bool bEnabled) override;
	virtual void SetTargetEntity(FSeam_EntityId Target) override;
	virtual FGameplayTag GetCurrentDecision() const override { return CurrentDecision; }
	virtual EAI_BrainBackend GetBackend() const override { return Backend; }
	//~ End IAI_Brain

	/** Broadcast locally after every decision change (authority + OnRep). */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatternsAI|Behavior")
	FAI_OnDecisionChanged OnDecisionChanged;

	/** BP-friendly read of the current decision tag. */
	UFUNCTION(BlueprintPure, Category = "DesignPatternsAI|Behavior")
	FGameplayTag BP_GetCurrentDecision() const { return CurrentDecision; }

	/** BP-friendly read of the active backend. */
	UFUNCTION(BlueprintPure, Category = "DesignPatternsAI|Behavior")
	EAI_BrainBackend BP_GetBackend() const { return Backend; }

	/** BP-callable, authority-guarded request to (re)evaluate the next decision. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatternsAI|Behavior")
	void BP_RequestDecision() { RequestDecision(); }

	/** BP-callable, authority-guarded enable/disable of decision-making. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatternsAI|Behavior")
	void BP_SetDecisionEnabled(bool bEnabled) { SetDecisionEnabled(bEnabled); }

	/** @return true if decision-making is currently enabled. */
	UFUNCTION(BlueprintPure, Category = "DesignPatternsAI|Behavior")
	bool IsDecisionEnabled() const { return bDecisionEnabled; }

	/** @return the current target entity id (invalid if none). */
	UFUNCTION(BlueprintPure, Category = "DesignPatternsAI|Behavior")
	FSeam_EntityId GetTargetEntity() const { return TargetEntity; }

	// ---- Config (data-driven; no magic numbers in code) ----

	/**
	 * Which backend this brain uses. StateMachine drives a core UDP_StateMachineComponent;
	 * BehaviorTree runs BehaviorTreeAsset through the owning pawn's AAIController.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatternsAI|Behavior")
	EAI_BrainBackend Backend = EAI_BrainBackend::StateMachine;

	/**
	 * BehaviorTree asset to run for the BehaviorTree backend. Ignored for the StateMachine backend.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatternsAI|Behavior")
	TObjectPtr<UBehaviorTree> BehaviorTreeAsset;

	/**
	 * Blackboard key the target entity id (as a string) is published under for both backends so
	 * states/tasks can read the current target without depending on this component.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatternsAI|Behavior")
	FName BlackboardKey_TargetEntity = TEXT("AI.TargetEntity");

	/**
	 * Blackboard key the BehaviorTree backend reads the decision tag back from (as an FName). When
	 * set, RequestDecision mirrors that key's value into CurrentDecision after ticking the tree.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatternsAI|Behavior")
	FName BlackboardKey_Decision = TEXT("AI.Decision");

	/** When true, decision changes are republished on the core bus (DP.Bus.AI.DecisionChanged). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatternsAI|Behavior")
	bool bBroadcastOnBus = true;

	/**
	 * Explicit FSM component to drive for the StateMachine backend. When null, the owner is searched
	 * for a UDP_StateMachineComponent at BeginPlay.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatternsAI|Behavior")
	TObjectPtr<UDP_StateMachineComponent> StateMachine;

protected:
	/**
	 * The authoritative decision tag. Replicated; clients react via OnRep. Cleared when decisions
	 * are disabled. This is the ONE replicated field on this component.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_CurrentDecision, VisibleInstanceOnly, BlueprintReadOnly, Category = "DesignPatternsAI|Behavior")
	FGameplayTag CurrentDecision;

	/** Whether decision-making is currently enabled. Authority-set; not replicated (derived effect is). */
	UPROPERTY(VisibleInstanceOnly, Category = "DesignPatternsAI|Behavior")
	bool bDecisionEnabled = true;

	/** Current target entity id (authority-set; pushed to the blackboard which other systems read). */
	UPROPERTY(VisibleInstanceOnly, Category = "DesignPatternsAI|Behavior")
	FSeam_EntityId TargetEntity;

	/** Client reaction to a replicated decision change: fire the delegate + bus. */
	UFUNCTION()
	void OnRep_CurrentDecision(FGameplayTag PreviousDecision);

	/**
	 * Bound (on authority) to the driven FSM's OnStateChanged so a state transition the FSM makes on
	 * its own keeps CurrentDecision in sync without an explicit RequestDecision call.
	 */
	UFUNCTION()
	void HandleFsmStateChanged(FGameplayTag From, FGameplayTag To);

private:
	/** Resolve (and cache) the owning pawn's AAIController for the BehaviorTree backend. */
	AAIController* ResolveAIController() const;

	/** Resolve the FSM component for the StateMachine backend (explicit or found on the owner). */
	UDP_StateMachineComponent* ResolveStateMachine() const;

	/** Set CurrentDecision on authority, replicate, and run shared change side effects. */
	void SetDecisionAuthoritative(FGameplayTag NewDecision);

	/** Shared decision-change reaction (delegate + optional bus) for both authority and OnRep. */
	void HandleDecisionChanged(FGameplayTag PreviousDecision, FGameplayTag NewDecision);

	/** Push the current target entity id (as an FName) into the relevant blackboard for the backend. */
	void PublishTargetToBlackboard();

	/** Resolve this agent's stable entity id via the identity seam (invalid if none). */
	FSeam_EntityId GetOwnerEntityId() const;

	/** True only if we own an actor and that actor has network authority. */
	bool HasAuthoritySafe() const;

	/** True once the BehaviorTree has been (re)started on the controller this session. */
	bool bBehaviorTreeRunning = false;
};
