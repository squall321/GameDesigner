// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "AI_PatrolRouteDataAsset.generated.h"

/**
 * How a patrol component traverses its route's waypoints.
 */
UENUM(BlueprintType)
enum class EAI_PatrolMode : uint8
{
	/** Walk to the last waypoint then stop. */
	Once,

	/** Walk to the last waypoint then jump back to the first (1 -> N -> 1 -> ...). */
	Loop,

	/** Walk to the last waypoint then reverse back to the first (1 -> N -> 1 reversed). */
	PingPong,

	/** Pick the next waypoint at random (never repeating the current). */
	Random
};

/**
 * One patrol WAYPOINT. Routes are defined RELATIVE to the patrol component's start transform so a route
 * asset can be shared across many sentries placed anywhere — the component composes each relative point
 * with its own spawn transform.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSAI_API FAI_PatrolWaypoint
{
	GENERATED_BODY()

	/** Waypoint location relative to the patrol component's start transform (local space). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Patrol")
	FVector RelativeLocation = FVector::ZeroVector;

	/** Seconds to wait at this waypoint before moving on (sentry pause). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Patrol", meta = (ClampMin = "0.0"))
	float WaitSeconds = 0.f;

	/** When true the agent performs a look-around (cosmetic scan) during its wait at this waypoint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Patrol")
	bool bLookAround = false;

	/** Optional yaw (degrees, relative to the start transform) to face while waiting here. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Patrol")
	float RelativeFaceYaw = 0.f;

	/** Optional tag classifying this waypoint (e.g. AI.Patrol.Guard) for designer logic. May be empty. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Patrol")
	FGameplayTag WaypointTag;

	FAI_PatrolWaypoint() = default;
};

/**
 * Patrol ROUTE definition: a mode + an ordered list of relative waypoints. Pure data, shareable across
 * sentries. The walker component (UAI_PatrolComponent) consumes it.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSAI_API UAI_PatrolRouteDataAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	/** How the route is traversed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Patrol")
	EAI_PatrolMode Mode = EAI_PatrolMode::Loop;

	/** Ordered waypoints (relative to the walker's start transform). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Patrol")
	TArray<FAI_PatrolWaypoint> Waypoints;
};
