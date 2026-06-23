// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AI/Navigation/NavigationTypes.h"
#include "Crowd/SimAg_Locomotion.h"
#include "SimAg_NavLocomotion.generated.h"

class USimAg_PathCacheSubsystem;

/**
 * Engine-NavigationSystem-backed implementation of the ISimAg_Locomotion seam. It drops into the existing
 * steering pipeline because USimAg_SteeringComponent resolves ISimAg_Locomotion off the owner.
 *
 * RequestMoveTo issues an ASYNC path request (through USimAg_PathCacheSubsystem so a crowd coalesces and
 * caches common routes), supporting PARTIAL paths (get as close as possible) and OFF-MESH LINKS (the
 * engine path includes them; we advance through them like any segment). The resolved FNavPathSharedPtr is
 * a PLAIN private member (NOT a UPROPERTY — nav paths are not UObjects).
 *
 * SetMovementInput blends the steering/avoidance desired velocity onto the movement substrate: it prefers
 * a UCharacterMovementComponent (AddMovementInput on the pawn), falling back to nudging the root
 * transform, so it works on a bare actor too without assuming AAIController.
 */
UCLASS(ClassGroup = (DesignPatternsSimAgents), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSSIMAGENTS_API USimAg_NavLocomotion : public UActorComponent, public ISimAg_Locomotion
{
	GENERATED_BODY()

public:
	USimAg_NavLocomotion();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent

	//~ Begin ISimAg_Locomotion
	virtual void RequestMoveTo_Implementation(const FVector& WorldGoal) override;
	virtual void SetMovementInput_Implementation(const FVector& WorldDesiredVelocity) override;
	//~ End ISimAg_Locomotion

	/** The current path's next steering point (where to head next along the path), or the goal if no path. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimAgents|Crowd")
	FVector GetNextPathPoint() const;

	/** True if the current path is a partial path (couldn't fully reach the goal). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimAgents|Crowd")
	bool IsPathPartial() const;

	/** True if a usable path is currently held. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimAgents|Crowd")
	bool HasPath() const;

	/** Distance (world units) at which the agent advances to the next path point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Crowd", meta = (ClampMin = "1.0"))
	float PathPointAcceptRadius = 80.f;

private:
	/** The current async path (plain shared pointer; nav paths are not UObjects, so NOT a UPROPERTY). */
	FNavPathSharedPtr CurrentPath;

	/** Index of the next point along CurrentPath the agent is steering toward. */
	int32 CurrentPathPoint = 0;

	/** The latest requested world goal (used when no nav path is available, and for re-requests). */
	FVector PendingGoal = FVector::ZeroVector;

	/** Whether a goal has been requested. */
	bool bHasGoal = false;

	/** Our in-flight path-cache request id, so a new request supersedes the old. */
	int32 ActivePathRequestId = 0;

	/** Weak handle to the path cache subsystem. */
	UPROPERTY(Transient)
	TWeakObjectPtr<USimAg_PathCacheSubsystem> CachedPathCache;

	/** Resolve (and cache) the path cache subsystem. Null-safe. */
	USimAg_PathCacheSubsystem* GetPathCache() const;

	/** Path-ready callback: adopt the resolved path and reset the point cursor. */
	void HandlePathReady(FNavPathSharedPtr Path);

	/** Advance CurrentPathPoint past any points the agent has already reached. */
	void AdvancePathCursor();
};
