// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DPPoolTypes.generated.h"

/**
 * Runtime configuration for a single pool, keyed by its pooled Class.
 *
 * This is the per-call/registration counterpart to FDP_DefaultPoolConfig (which is the
 * settings-authored, soft-class form warmed automatically on world init). Registering a pool
 * with a Class that already has a pool merges/updates the config rather than creating a second.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNS_API FDP_PoolConfig
{
	GENERATED_BODY()

	/** Concrete class to pool. May be a plain UObject or an AActor subclass. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Pool")
	TSubclassOf<UObject> Class;

	/** Instances pre-created during warmup. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Pool", meta = (ClampMin = "0"))
	int32 InitialSize = 8;

	/** Soft cap on idle instances; idle instances beyond this are eligible for eviction. 0 = unbounded. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Pool", meta = (ClampMin = "0"))
	int32 SoftCap = 64;

	/** Whether the pool may create new instances when exhausted. If false, Acquire returns null. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Pool")
	bool bAllowGrow = true;

	/** Seconds an idle instance may sit past the soft cap before being evicted. 0 = never evict. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Pool", meta = (ClampMin = "0.0"))
	float IdleEvictSeconds = 30.f;

	FDP_PoolConfig() = default;

	explicit FDP_PoolConfig(TSubclassOf<UObject> InClass)
		: Class(InClass)
	{
	}
};

/**
 * Fired when an asynchronous (frame-spread) warmup finishes pre-creating its instances.
 * Param: the class whose pool finished warming, and how many instances are now idle.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FDP_OnWarmupComplete,
	TSubclassOf<UObject>, PooledClass,
	int32, IdleCount);
