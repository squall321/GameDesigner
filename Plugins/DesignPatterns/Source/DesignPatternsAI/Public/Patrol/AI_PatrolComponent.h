// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Identity/Seam_EntityId.h"
#include "Patrol/AI_PatrolRouteDataAsset.h"
#include "AI_PatrolComponent.generated.h"

class UAI_PerceptionComponent;
class IDP_BlackboardProvider;
struct FAI_Percept;

/**
 * Route WALKER + sentry. Advances through a UAI_PatrolRouteDataAsset's waypoints (honoring per-waypoint
 * wait + look-around), and on a fresh percept INVESTIGATES the last-known location before returning to
 * the route. It pushes the current go-to into the agent blackboard (BlackboardKey_PatrolGoal) so the FSM/
 * movement drives there, and calls IAI_Brain::SetTargetEntity on engagement so the brain takes over.
 *
 * AUTHORITY: walking + state advance + blackboard writes are SERVER-ONLY (guarded at the TOP). The only
 * replicated field is CurrentWaypointIndex (so clients can cosmetically interpolate / show the route
 * progress); the authoritative goal lives in the server blackboard.
 */
UCLASS(ClassGroup = (DesignPatternsAI), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSAI_API UAI_PatrolComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAI_PatrolComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent

	/**
	 * Begin patrolling Route (or the default Route if null). AUTHORITY ONLY. Captures the current owner
	 * transform as the route's start transform and resets to waypoint 0.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|AI|Patrol")
	void StartPatrol(UAI_PatrolRouteDataAsset* Route);

	/** Stop patrolling (clears the blackboard goal). AUTHORITY ONLY. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|AI|Patrol")
	void StopPatrol();

	/**
	 * Divert to investigate WorldLocation, then resume the route. AUTHORITY ONLY. Called automatically on a
	 * fresh percept, and callable by designers/scripting.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|AI|Patrol")
	void InvestigateLocation(FVector WorldLocation);

	/** @return the current waypoint index (replicated for cosmetic client interp). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|AI|Patrol")
	int32 GetCurrentWaypointIndex() const { return CurrentWaypointIndex; }

	/** @return true while patrolling (vs. stopped or engaged). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|AI|Patrol")
	bool IsPatrolling() const { return bPatrolling; }

	// ---- Config (tunables; no magic gameplay numbers in code) ----

	/** Route used by StartPatrol when none is supplied (and auto-started in BeginPlay if bAutoStart). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Patrol")
	TObjectPtr<UAI_PatrolRouteDataAsset> DefaultRoute;

	/** When true, the component begins patrolling DefaultRoute automatically on BeginPlay (authority). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Patrol")
	bool bAutoStart = true;

	/** Distance (world units) within which a waypoint is considered "reached". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Patrol", meta = (ClampMin = "1.0"))
	float AcceptanceRadius = 100.f;

	/** Seconds to linger at an investigation point before resuming the route. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Patrol", meta = (ClampMin = "0.0"))
	float InvestigateLingerSeconds = 4.f;

	/** When a percept of at least this strength arrives, auto-investigate its last-known location. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Patrol", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float InvestigatePerceptThreshold = 0.3f;

	/** Blackboard key the current patrol go-to world location is written under (vector). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Patrol")
	FName BlackboardKey_PatrolGoal = TEXT("AI.PatrolGoal");

protected:
	/** Current waypoint index. Replicated for cosmetic client interpolation only. */
	UPROPERTY(ReplicatedUsing = OnRep_Waypoint, VisibleInstanceOnly, BlueprintReadOnly, Category = "DesignPatterns|AI|Patrol")
	int32 CurrentWaypointIndex = 0;

	/** Client reaction to a replicated waypoint change (cosmetic only). */
	UFUNCTION()
	void OnRep_Waypoint();

private:
	/** Active route (authority bookkeeping). */
	UPROPERTY(Transient)
	TObjectPtr<UAI_PatrolRouteDataAsset> ActiveRoute;

	/** The owner transform captured at StartPatrol; relative waypoints compose with this. */
	FTransform StartTransform = FTransform::Identity;

	/** Whether currently patrolling. */
	bool bPatrolling = false;

	/** Direction for PingPong mode (+1 forward, -1 back). */
	int32 PingPongDir = 1;

	/** True while pausing at a waypoint / investigation point; counts WaitTimer down before advancing. */
	bool bWaiting = false;

	/** Remaining wait time at the current waypoint / investigation point. */
	float WaitTimer = 0.f;

	/** True while diverted to an investigation point (return to route afterward). */
	bool bInvestigating = false;

	/** The world investigation point while bInvestigating. */
	FVector InvestigateTarget = FVector::ZeroVector;

	/** Resolve the owner's perception component (to bind OnPerceptUpdated + read last-known). */
	UAI_PerceptionComponent* ResolvePerception() const;

	/** Resolve the owner's blackboard provider. */
	IDP_BlackboardProvider* ResolveBlackboardProvider() const;

	/** Compute the absolute world location of waypoint Index (composed with StartTransform). */
	FVector GetWaypointWorldLocation(int32 Index) const;

	/** Advance CurrentWaypointIndex according to the route mode (authority). */
	void AdvanceWaypoint();

	/** Push the current go-to world location into the blackboard (authority). */
	void PushGoalToBlackboard(const FVector& WorldGoal);

	/** Bound to the perception component: investigate strong fresh percepts and engage. */
	UFUNCTION()
	void HandlePerceptUpdated(const FAI_Percept& Percept);

	/** Resolve the owner's stable entity id via the identity seam (invalid if none). */
	FSeam_EntityId GetOwnerEntityId() const;

	/** True only if we own an actor and that actor has network authority. */
	bool HasAuthoritySafe() const;
};
