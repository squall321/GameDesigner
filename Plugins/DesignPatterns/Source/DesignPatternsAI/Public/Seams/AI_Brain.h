// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Identity/Seam_EntityId.h"
#include "AI_Brain.generated.h"

/**
 * Which backend a brain uses to produce decisions.
 *
 * The behavior bridge (UAI_BehaviorComponent) selects one of these from data and routes
 * RequestDecision() to the matching engine: a core FSM component or an engine BehaviorTree run
 * through an AAIController. Keeping this an enum (not a class hierarchy) lets a single component
 * implement IAI_Brain and switch backends without changing its callers.
 */
UENUM(BlueprintType)
enum class EAI_BrainBackend : uint8
{
	/** No backend selected / brain disabled. RequestDecision() is a no-op. */
	None,

	/** Decisions come from a core UDP_StateMachineComponent's active-state tag. */
	StateMachine,

	/** Decisions come from an engine UBehaviorTree run via an AAIController. */
	BehaviorTree
};

/**
 * Brain seam: the decision-making contract a tactical agent exposes to the rest of the game.
 *
 * Implemented by UAI_BehaviorComponent. Other systems (a spawn director, a squad coordinator,
 * the message bus, designer scripting) drive an agent's high-level decision through THIS interface
 * without depending on whether the backend is an FSM or a BehaviorTree, and without including the
 * AI module's concrete component type.
 *
 * AUTHORITY: RequestDecision / SetDecisionEnabled / SetTargetEntity are AUTHORITY-ONLY — the
 * implementation guards them at the top and no-ops on clients. Decisions are authoritative gameplay
 * state; the selected tag replicates down to clients (cosmetic reactions read it via OnRep / the
 * message bus). The read accessors (GetCurrentDecision / GetBackend) are safe on any machine.
 */
UINTERFACE(MinimalAPI, BlueprintType, meta = (CannotImplementInterfaceInBlueprint))
class UAI_Brain : public UInterface
{
	GENERATED_BODY()
};

/** @see UAI_Brain */
class DESIGNPATTERNSAI_API IAI_Brain
{
	GENERATED_BODY()

public:
	/**
	 * Ask the brain to (re)evaluate and select its next decision now. AUTHORITY ONLY.
	 *
	 * For the FSM backend this evaluates the active state's transitions; for the BehaviorTree
	 * backend this (re)starts/ticks the tree on the controller. The resulting decision tag is
	 * published via GetCurrentDecision() and replicated to clients.
	 */
	virtual void RequestDecision() = 0;

	/**
	 * Enable or disable decision-making. AUTHORITY ONLY. When disabled the backend is paused
	 * (FSM transitions stop being evaluated / the BehaviorTree is stopped) and the current
	 * decision tag is cleared.
	 */
	virtual void SetDecisionEnabled(bool bEnabled) = 0;

	/**
	 * Set the entity this brain should treat as its current target. AUTHORITY ONLY.
	 * The implementation publishes the target into the agent blackboard / BehaviorTree blackboard
	 * so states and tasks can read it.
	 */
	virtual void SetTargetEntity(FSeam_EntityId Target) = 0;

	/** @return the tag of the decision the brain has currently selected (invalid when disabled/none). */
	virtual FGameplayTag GetCurrentDecision() const = 0;

	/** @return which backend this brain is currently using to produce decisions. */
	virtual EAI_BrainBackend GetBackend() const = 0;
};
