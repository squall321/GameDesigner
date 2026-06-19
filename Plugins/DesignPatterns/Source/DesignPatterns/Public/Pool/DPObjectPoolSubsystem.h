// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "Pool/DPPoolTypes.h"
#include "Engine/TimerHandle.h"
#include "DPObjectPoolSubsystem.generated.h"

class AActor;

/**
 * State for a single class's pool. Held by value in the subsystem's Pools map.
 *
 * Declared at file scope (UHT does not support USTRUCTs nested inside a UCLASS) but treated as
 * an implementation detail of UDP_ObjectPoolSubsystem — not part of the public Blueprint API.
 */
USTRUCT()
struct FDP_PoolState
{
	GENERATED_BODY()

	/** Configuration for this pool. */
	UPROPERTY()
	FDP_PoolConfig Config;

	/** Idle, ready-to-hand-out instances. UPROPERTY keeps them GC-rooted while pooled. */
	UPROPERTY()
	TArray<TObjectPtr<UObject>> Idle;

	/** Currently checked-out instances. Also UPROPERTY so the pool keeps ownership. */
	UPROPERTY()
	TArray<TObjectPtr<UObject>> Live;

	/** World real-time seconds each idle instance became idle; parallel to Idle. */
	UPROPERTY()
	TArray<double> IdleSince;

	/** Highest simultaneous live count observed (peak), for stats/debug. */
	UPROPERTY()
	int32 PeakLive = 0;
};

/**
 * World-scoped object pool: recycles UObject/AActor instances instead of spawning and
 * destroying them, removing per-spawn allocation, construction and GC churn from hot paths
 * (projectiles, FX, pickups, enemies).
 *
 * Lifetime & scope
 * ----------------
 * Lives on the UWorld, so every pool — and every instance it owns — is torn down with the
 * level. Owned instances are held in UPROPERTY TObjectPtr containers so they are GC-rooted
 * while pooled and never collected out from under the pool.
 *
 * Reset contract (IMPORTANT)
 * --------------------------
 * The pool reuses instances, so it can only guarantee a *structural* reset. On release of an
 * AActor it applies a safe default: zeroes velocity, restores the registered collision/visibility/
 * tick state, hides the actor and disables collision & tick. On acquire it reverses that and
 * places the actor at the requested transform. It does NOT and cannot reset gameplay/latent
 * state — active timers, Gameplay Abilities, montages, Niagara, AI, delegate bindings. Pooled
 * classes MUST implement IDP_Poolable and reset that state themselves in OnReturnedToPool /
 * OnAcquiredFromPool. The subsystem invokes those hooks if the class implements the interface.
 *
 * Tick policy
 * -----------
 * The subsystem is deliberately NOT an FTickableGameObject. Frame-spread warmup and idle
 * eviction are driven by the world's FTimerManager so nothing ticks in editor/preview worlds.
 */
UCLASS()
class DESIGNPATTERNS_API UDP_ObjectPoolSubsystem : public UDP_WorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	// ---- Pool management ----

	/**
	 * Create (or update the config of) a pool for Config.Class. Does not pre-create instances;
	 * call WarmupAsync or simply Acquire to populate it. Safe to call repeatedly.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Pool")
	void RegisterPool(const FDP_PoolConfig& Config);

	/** True if a pool has been registered for the given class. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Pool")
	bool HasPool(TSubclassOf<UObject> Class) const;

	/**
	 * Acquire a pooled UObject of the given class. Reuses an idle instance if available, else
	 * grows the pool (if allowed / not configured). Calls IDP_Poolable::OnAcquiredFromPool if
	 * implemented. Returns null if the class is abstract, growth is disallowed and the pool is
	 * empty, or allocation fails. For actors prefer AcquireActor.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Pool", meta = (DeterminesOutputType = "Class"))
	UObject* Acquire(TSubclassOf<UObject> Class);

	/**
	 * Acquire a pooled actor, place it at Transform, set its Owner, then activate it (un-hide,
	 * restore collision/tick) and fire OnAcquiredFromPool. The actor is reused, not re-spawned,
	 * so BeginPlay does NOT run again — do per-use setup in OnAcquiredFromPool.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Pool", meta = (DeterminesOutputType = "Class"))
	AActor* AcquireActor(TSubclassOf<AActor> Class, const FTransform& Transform, AActor* Owner = nullptr);

	/**
	 * Return an instance to its pool. Fires OnReturnedToPool, applies the safe default
	 * deactivation for actors, and parks the instance as idle for reuse. Double-release is a
	 * no-op. The instance must not be used by the caller after this returns.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Pool")
	void Release(UObject* Instance);

	/**
	 * Release the instance after DelaySeconds using the world FTimerManager. If the instance is
	 * destroyed or the world torn down first, the timer simply no-ops. DelaySeconds <= 0 releases
	 * immediately.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Pool")
	void ReleaseAfter(UObject* Instance, float DelaySeconds);

	/**
	 * Pre-create up to Count instances for Class, spread across frames (InstancesPerFrame each
	 * tick of a world timer) to avoid a hitch. Fires OnWarmupComplete when done. Registers a
	 * default pool for Class if one does not yet exist.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Pool", meta = (AdvancedDisplay = "InstancesPerFrame"))
	void WarmupAsync(TSubclassOf<UObject> Class, int32 Count, int32 InstancesPerFrame = 4);

	/**
	 * Destroy/discard every instance (idle and live) of Class's pool and remove the pool.
	 * Live instances are released first. Use on level-section unload to reclaim memory.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Pool")
	void DrainPool(TSubclassOf<UObject> Class);

	/** Number of instances currently checked out (live) for a class. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Pool")
	int32 GetLiveCount(TSubclassOf<UObject> Class) const;

	/** Number of idle (available) instances for a class. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Pool")
	int32 GetIdleCount(TSubclassOf<UObject> Class) const;

	/** Fired whenever an async warmup completes. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Pool")
	FDP_OnWarmupComplete OnWarmupComplete;

	//~ Begin UDP_WorldSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_WorldSubsystem

private:
	/** Per-class pool storage. UPROPERTY so all owned instances stay reachable by GC. */
	UPROPERTY()
	TMap<TSubclassOf<UObject>, FDP_PoolState> Pools;

	/** Pending frame-spread warmups, keyed by class. */
	struct FWarmupJob
	{
		TSubclassOf<UObject> Class;
		int32 Remaining = 0;
		int32 PerFrame = 4;
		FTimerHandle TimerHandle;
	};
	TArray<TSharedPtr<FWarmupJob>> WarmupJobs;

	/** Single shared timer that drives idle eviction sweeps. */
	FTimerHandle EvictionTimerHandle;

	/**
	 * In-flight ReleaseAfter() timers, keyed by a monotonic id. Tracked so they can be cancelled
	 * in Deinitialize() (the subsystem must own every timer it creates). Each one-shot timer
	 * removes its own entry when it fires, so this map never grows without bound.
	 */
	TMap<int32, FTimerHandle> PendingReleaseTimers;

	/** Monotonic id source for PendingReleaseTimers keys. */
	int32 NextReleaseTimerId = 1;

	/** Resolve the pool for a class, creating a default one if missing. */
	FDP_PoolState& FindOrAddPool(TSubclassOf<UObject> Class);

	/** Construct one brand-new instance for the given pool (NewObject / SpawnActor deferred-inactive). */
	UObject* CreateInstance(TSubclassOf<UObject> Class);

	/** Apply the safe default activation (actors only): place, un-hide, restore collision/tick. */
	void ActivateInstance(UObject* Instance, const FTransform& Transform, AActor* Owner);

	/** Apply the safe default deactivation (actors only): zero velocity, hide, disable collision/tick. */
	void DeactivateInstance(UObject* Instance);

	/** Fire IDP_Poolable::OnAcquiredFromPool if implemented. */
	static void NotifyAcquired(UObject* Instance);

	/** Fire IDP_Poolable::OnReturnedToPool if implemented. */
	static void NotifyReturned(UObject* Instance);

	/** Query IDP_Poolable::CanBeReclaimed (returns true when the instance does not implement IDP_Poolable). */
	static bool CanReclaim(const UObject* Instance);

	/** Timer callback: advance every warmup job by its per-frame budget. */
	void TickWarmup(TWeakPtr<FWarmupJob> WeakJob);

	/** Timer callback: evict idle instances past soft cap / idle-evict time. */
	void EvictionSweep();

	/** Ensure the recurring eviction timer is running (if any pool wants eviction). */
	void EnsureEvictionTimer();

	/** Destroy a single instance for good (DestroyActor or MarkAsGarbage). */
	void DestroyInstance(UObject* Instance);
};
