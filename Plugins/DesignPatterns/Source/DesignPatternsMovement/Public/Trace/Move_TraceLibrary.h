// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CollisionQueryParams.h"
#include "Move_TraceLibrary.generated.h"

class ACharacter;

/**
 * Tuning bundle for a ledge/wall query. Carried by reference into the trace helpers so the trace
 * numbers come from data (a UMove_LocomotionProfile / settings), never hardcoded in the helper.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSMOVEMENT_API FMove_LedgeTuning
{
	GENERATED_BODY()

	/** How far forward (cm) to look for an obstacle/ledge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Trace", meta = (ClampMin = "0.0"))
	float ForwardReach = 80.f;

	/** Highest ledge (cm above the capsule base) the character can mantle onto. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Trace", meta = (ClampMin = "0.0"))
	float MaxMantleHeight = 200.f;

	/** Below this obstacle height (cm) a low wall is vaulted rather than mantled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Trace", meta = (ClampMin = "0.0"))
	float VaultMaxHeight = 90.f;

	/** Minimum clear depth (cm) on top of the ledge needed to stand (mantle) or land beyond (vault). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Trace", meta = (ClampMin = "0.0"))
	float RequiredClearDepth = 50.f;

	/** Trace channels treated as solid for the query. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Trace")
	TArray<TEnumAsByte<ECollisionChannel>> Channels;
};

/** Result of a ledge query: whether a ledge was found, its world-space top, and the kind of traversal. */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSMOVEMENT_API FMove_LedgeResult
{
	GENERATED_BODY()

	/** True if a traversable ledge/obstacle was found. */
	UPROPERTY(BlueprintReadOnly, Category = "Movement|Trace")
	bool bFound = false;

	/** True if the obstacle is low enough to vault (otherwise mantle). Only meaningful when bFound. */
	UPROPERTY(BlueprintReadOnly, Category = "Movement|Trace")
	bool bIsVault = false;

	/** World-space point on top of the ledge the character should pull up to / vault onto. */
	UPROPERTY(BlueprintReadOnly, Category = "Movement|Trace")
	FVector LedgeTopLocation = FVector::ZeroVector;

	/** Outward-facing normal of the wall (toward the character). */
	UPROPERTY(BlueprintReadOnly, Category = "Movement|Trace")
	FVector WallNormal = FVector::ZeroVector;

	/** Final transform the character interpolates to (location at the ledge top, facing into the wall). */
	UPROPERTY(BlueprintReadOnly, Category = "Movement|Trace")
	FTransform TargetTransform = FTransform::Identity;

	/** Height (cm) of the obstacle above the capsule base. */
	UPROPERTY(BlueprintReadOnly, Category = "Movement|Trace")
	float LedgeHeight = 0.f;
};

/** Result of a lateral wall query (for wall-run / climb attach). */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSMOVEMENT_API FMove_WallResult
{
	GENERATED_BODY()

	/** True if a near-vertical wall was found on the queried side. */
	UPROPERTY(BlueprintReadOnly, Category = "Movement|Trace")
	bool bFound = false;

	/** Impact point on the wall. */
	UPROPERTY(BlueprintReadOnly, Category = "Movement|Trace")
	FVector ImpactPoint = FVector::ZeroVector;

	/** Wall surface normal (points away from the wall, toward the character). */
	UPROPERTY(BlueprintReadOnly, Category = "Movement|Trace")
	FVector WallNormal = FVector::ZeroVector;

	/** +1 if the wall is on the character's right, -1 if on the left, 0 if directly ahead/unknown. */
	UPROPERTY(BlueprintReadOnly, Category = "Movement|Trace")
	int32 Side = 0;
};

/**
 * Stateless trace helpers for traversal queries. All numbers come in via FMove_LedgeTuning, so callers
 * (states / the server validator) feed authored data and the helper never invents thresholds. Every
 * trace ignores the querying character. These run identically on server (authoritative re-validation)
 * and client (cosmetic prediction), which is exactly why they are pure free functions.
 */
UCLASS()
class DESIGNPATTERNSMOVEMENT_API UMove_TraceLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Trace for a ledge/obstacle directly in front of Character and classify it as mantle or vault.
	 * @param Character the querying character (its capsule + forward vector seed the trace; ignored by it).
	 * @param Tuning    the authored reach/height/depth thresholds and channels.
	 * @return a populated FMove_LedgeResult (bFound=false if nothing traversable was found).
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Movement|Trace", meta = (WorldContext = "Character"))
	static FMove_LedgeResult FindLedge(const ACharacter* Character, const FMove_LedgeTuning& Tuning);

	/**
	 * Trace laterally for a wall on the given side for wall-run/climb attach.
	 * @param Character   the querying character.
	 * @param Distance    lateral trace distance (cm).
	 * @param bRightSide  true to query the right side, false the left.
	 * @param Channels    trace channels treated as solid.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Movement|Trace", meta = (WorldContext = "Character"))
	static FMove_WallResult FindWall(const ACharacter* Character, float Distance, bool bRightSide,
		const TArray<TEnumAsByte<ECollisionChannel>>& Channels);

	/** True if the character's capsule origin is currently inside a physics water volume. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Movement|Trace", meta = (WorldContext = "Character"))
	static bool IsInWater(const ACharacter* Character);

	/**
	 * Sample the floor slope angle (degrees) under the character. Returns 0 when not on a floor.
	 * Used by the slide state to decide whether a slope is steep enough to accelerate.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Movement|Trace", meta = (WorldContext = "Character"))
	static float GetFloorSlopeDegrees(const ACharacter* Character);

private:
	/** Build a query-params block that ignores the character (and its attached actors). */
	static FCollisionQueryParams MakeIgnoreParams(const ACharacter* Character);

	/** Convert the (possibly empty) channel list into an object query, defaulting to WorldStatic. */
	static FCollisionObjectQueryParams MakeObjectParams(const TArray<TEnumAsByte<ECollisionChannel>>& Channels);
};
