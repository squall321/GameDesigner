// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UObject/ScriptInterface.h"
#include "SimAg_SteeringComponent.generated.h"

class ISimAg_Locomotion;
class ISimAg_FlowField;
class IDP_BlackboardProvider;
class USimAg_FlowFieldSubsystem;

/**
 * Crowd steering for one agent: turns a move target (a world point, taken from the brain's blackboard
 * MoveTarget key) into a per-frame desired velocity by combining flow-field guidance with crowd
 * separation, and applies it through the ISimAg_Locomotion seam.
 *
 * SUBSTRATE-AGNOSTIC: it never assumes AAIController or a specific movement component. It resolves the
 * locomotion seam off its owning actor (the owner, or a sibling component, that implements
 * ISimAg_Locomotion); if none is found it falls back to nudging the owner's root transform directly so
 * the component is still useful on a bare actor.
 *
 * The move target is authority-derived: SetMoveTarget early-returns on clients (the brain runs on the
 * server and the target rides the agent's replicated state). Steering math itself runs locally on every
 * machine so movement looks smooth, but the GOAL is never set by a client.
 */
UCLASS(ClassGroup = (DesignPatterns), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSSIMAGENTS_API USimAg_SteeringComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USimAg_SteeringComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent

	/**
	 * Set the world-space goal this agent steers toward. AUTHORITY ONLY: early-returns on clients so the
	 * goal is always server-derived. Also pushed through the locomotion seam's RequestMoveTo so a
	 * path-following substrate can plan immediately.
	 */
	UFUNCTION(BlueprintCallable, Category = "SimAgents|Crowd")
	void SetMoveTarget(const FVector& WorldGoal);

	/** Clear the current goal; the agent stops issuing movement input. AUTHORITY ONLY. */
	UFUNCTION(BlueprintCallable, Category = "SimAgents|Crowd")
	void ClearMoveTarget();

	/** True if a goal is currently set and not yet reached. */
	UFUNCTION(BlueprintPure, Category = "SimAgents|Crowd")
	bool HasMoveTarget() const { return bHasTarget; }

	/** The current world goal (only meaningful when HasMoveTarget()). */
	UFUNCTION(BlueprintPure, Category = "SimAgents|Crowd")
	FVector GetMoveTarget() const { return MoveTarget; }

	/** The owning agent's current world location, used by the flow-field subsystem for separation. */
	FVector GetAgentLocation() const;

	/** Neighbour-search radius this agent contributes / uses for separation (from settings unless set). */
	float GetSeparationRadius() const { return SeparationRadius; }

	/**
	 * Blackboard key the brain writes the move target into (an FVector). The steering component reads it
	 * on authority each tick and mirrors it into its goal, so the brain and steering stay decoupled.
	 * Defaults to "MoveTarget"; designer-overridable to share a key convention with other systems.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Crowd")
	FName MoveTargetKey = TEXT("MoveTarget");

	/** Blackboard bool key set true while the agent has an active goal (for guards/animation). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Crowd")
	FName MovingKey = TEXT("IsMoving");

	/**
	 * Neighbour-search radius for separation. <= 0 means "use the settings default". Exposed so a large
	 * agent can claim more personal space without touching code.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Crowd", meta = (ClampMin = "0.0"))
	float SeparationRadius = 0.f;

private:
	/** Current world goal; valid only while bHasTarget. */
	UPROPERTY(Transient)
	FVector MoveTarget = FVector::ZeroVector;

	/** Whether a goal is currently set. */
	UPROPERTY(Transient)
	bool bHasTarget = false;

	/** Seconds since the last steering recompute, for the settings-driven cadence. */
	float SteeringAccumulator = 0.f;

	/** Cached steering period (1 / SteeringTickHz) from settings. */
	float SteeringPeriod = 0.1f;

	/** Cached arrival radius from settings. */
	float ArrivalRadius = 60.f;

	/** Cached separation weight from settings. */
	float SeparationWeight = 0.6f;

	/**
	 * Locomotion seam implementer for this agent (owner or a sibling component). Non-owning: held as a
	 * script interface and re-resolved if it goes stale. May be empty (then we nudge the root directly).
	 */
	UPROPERTY(Transient)
	TScriptInterface<ISimAg_Locomotion> Locomotion;

	/** The world flow-field subsystem (fallback guidance/separation). Weak, re-resolved if stale. */
	UPROPERTY(Transient)
	TWeakObjectPtr<USimAg_FlowFieldSubsystem> FlowField;

	/** True on the server / standalone (mirrors the world-authority rule). */
	bool HasOwnerAuthority() const;

	/** Find an ISimAg_Locomotion implementer on the owner (the actor or one of its components). */
	void ResolveLocomotion();

	/** Read the brain's MoveTarget blackboard key (authority) and mirror it into the goal. */
	void SyncTargetFromBlackboard();

	/** Compute and apply the desired velocity for this step toward the current goal. */
	void StepSteering();

	/** Apply a desired world velocity through the locomotion seam, or nudge the root if none. */
	void ApplyDesiredVelocity(const FVector& WorldDesiredVelocity);

	/** Resolve the blackboard provider off the owner's agent component, if any. */
	TScriptInterface<IDP_BlackboardProvider> ResolveBlackboard() const;
};
