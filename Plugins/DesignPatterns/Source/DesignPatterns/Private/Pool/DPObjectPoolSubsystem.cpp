// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Pool/DPObjectPoolSubsystem.h"
#include "Pool/DPPoolable.h"
#include "Core/DPLog.h"
#include "Core/DPDeveloperSettings.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/MovementComponent.h"
#include "Components/PrimitiveComponent.h"
#include "TimerManager.h"
#include "Stats/Stats.h"

// ---- Stats: hot-path cycle counters and live/idle/peak/evicted accumulators ----------------
DECLARE_CYCLE_STAT(TEXT("Pool Acquire"), STAT_DPPoolAcquire, STATGROUP_DesignPatterns);
DECLARE_CYCLE_STAT(TEXT("Pool Release"), STAT_DPPoolRelease, STATGROUP_DesignPatterns);
DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("Pool Live"), STAT_DPPoolLive, STATGROUP_DesignPatterns);
DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("Pool Idle"), STAT_DPPoolIdle, STATGROUP_DesignPatterns);
DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("Pool Peak Live"), STAT_DPPoolPeak, STATGROUP_DesignPatterns);
DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("Pool Evicted"), STAT_DPPoolEvicted, STATGROUP_DesignPatterns);

namespace
{
	/** How often the idle-eviction sweep runs, in seconds. */
	constexpr float GDPPoolEvictionPeriod = 1.0f;

	/** Running total of instances evicted this session (mirrored into STAT_DPPoolEvicted). */
	int32 GDPPoolEvictedTotal = 0;
}

// ============================================================================================
// Lifecycle
// ============================================================================================

void UDP_ObjectPoolSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (const UDP_DeveloperSettings* Settings = UDP_DeveloperSettings::Get())
	{
		bEnableVerboseLogging = Settings->bVerboseLoggingByDefault;

		// Warm every project-configured default pool. PooledClass is a soft class, so we load
		// it synchronously here (world init, not a hot path) before registering its pool.
		for (const FDP_DefaultPoolConfig& Default : Settings->DefaultPools)
		{
			if (Default.PooledClass.IsNull())
			{
				continue;
			}

			UClass* LoadedClass = Default.PooledClass.LoadSynchronous();
			if (!LoadedClass)
			{
				UE_LOG(LogDPPool, Warning, TEXT("Default pool skipped: class '%s' failed to load."),
					*Default.PooledClass.ToString());
				continue;
			}

			FDP_PoolConfig Config(LoadedClass);
			Config.InitialSize = Default.InitialSize;
			Config.SoftCap = Default.SoftCap;
			Config.bAllowGrow = Default.bAllowGrow;
			Config.IdleEvictSeconds = Default.IdleEvictSeconds;

			RegisterPool(Config);
			if (Config.InitialSize > 0)
			{
				WarmupAsync(LoadedClass, Config.InitialSize);
			}
		}
	}

	EnsureEvictionTimer();

	UE_LOG(LogDPPool, Verbose, TEXT("Object pool initialized with %d default pool(s)."), Pools.Num());
}

void UDP_ObjectPoolSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		FTimerManager& TM = World->GetTimerManager();
		TM.ClearTimer(EvictionTimerHandle);
		for (const TSharedPtr<FWarmupJob>& Job : WarmupJobs)
		{
			if (Job.IsValid())
			{
				TM.ClearTimer(Job->TimerHandle);
			}
		}
		// Cancel any ReleaseAfter() timers that have not yet fired.
		for (TPair<int32, FTimerHandle>& Pending : PendingReleaseTimers)
		{
			TM.ClearTimer(Pending.Value);
		}
	}
	WarmupJobs.Reset();
	PendingReleaseTimers.Reset();

	// Explicitly destroy owned actors; plain UObjects are collected once Pools is cleared.
	for (TPair<TSubclassOf<UObject>, FDP_PoolState>& Pair : Pools)
	{
		for (TObjectPtr<UObject>& Obj : Pair.Value.Live)
		{
			DestroyInstance(Obj);
		}
		for (TObjectPtr<UObject>& Obj : Pair.Value.Idle)
		{
			DestroyInstance(Obj);
		}
	}
	Pools.Reset();

	Super::Deinitialize();
}

// ============================================================================================
// Pool management
// ============================================================================================

FDP_PoolState& UDP_ObjectPoolSubsystem::FindOrAddPool(TSubclassOf<UObject> Class)
{
	if (FDP_PoolState* Existing = Pools.Find(Class))
	{
		return *Existing;
	}
	FDP_PoolState& New = Pools.Add(Class);
	New.Config = FDP_PoolConfig(Class);
	return New;
}

void UDP_ObjectPoolSubsystem::RegisterPool(const FDP_PoolConfig& Config)
{
	if (!*Config.Class)
	{
		UE_LOG(LogDPPool, Warning, TEXT("RegisterPool ignored: null Class."));
		return;
	}
	if (Config.Class->HasAnyClassFlags(CLASS_Abstract))
	{
		UE_LOG(LogDPPool, Warning, TEXT("RegisterPool ignored: '%s' is abstract."), *Config.Class->GetName());
		return;
	}

	FDP_PoolState& Pool = FindOrAddPool(Config.Class);
	Pool.Config = Config; // update/merge config for an existing pool

	if (Config.IdleEvictSeconds > 0.f)
	{
		EnsureEvictionTimer();
	}

	UE_LOG(LogDPPool, Verbose, TEXT("Registered pool for '%s' (initial=%d, softcap=%d, grow=%s, evict=%.1fs)."),
		*Config.Class->GetName(), Config.InitialSize, Config.SoftCap,
		Config.bAllowGrow ? TEXT("true") : TEXT("false"), Config.IdleEvictSeconds);
}

bool UDP_ObjectPoolSubsystem::HasPool(TSubclassOf<UObject> Class) const
{
	return *Class && Pools.Contains(Class);
}

// ============================================================================================
// Acquire / Release
// ============================================================================================

UObject* UDP_ObjectPoolSubsystem::CreateInstance(TSubclassOf<UObject> Class)
{
	if (!*Class)
	{
		return nullptr;
	}

	UWorld* World = GetWorld();

	if (Class->IsChildOf(AActor::StaticClass()))
	{
		if (!World)
		{
			return nullptr;
		}
		// Spawn deferred & inactive: the actor exists but is parked (hidden, no collision/tick)
		// until ActivateInstance places and wakes it. Deferred spawn lets us deactivate before
		// BeginPlay so it never "flashes" live in the world on its first creation.
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.ObjectFlags |= RF_Transient;

		AActor* Actor = World->SpawnActor<AActor>(*Class, FTransform::Identity, Params);
		if (Actor)
		{
			DeactivateInstance(Actor);
		}
		return Actor;
	}

	// Plain UObject: the subsystem (or its world) is a safe Outer so the instance shares world
	// lifetime and is GC-rooted via the Pools UPROPERTY.
	UObject* Outer = World ? static_cast<UObject*>(World) : static_cast<UObject*>(this);
	return NewObject<UObject>(Outer, *Class, NAME_None, RF_Transient);
}

UObject* UDP_ObjectPoolSubsystem::Acquire(TSubclassOf<UObject> Class)
{
	SCOPE_CYCLE_COUNTER(STAT_DPPoolAcquire);

	if (!*Class)
	{
		UE_LOG(LogDPPool, Warning, TEXT("Acquire ignored: null Class."));
		return nullptr;
	}
	if (Class->HasAnyClassFlags(CLASS_Abstract))
	{
		UE_LOG(LogDPPool, Warning, TEXT("Acquire ignored: '%s' is abstract."), *Class->GetName());
		return nullptr;
	}

	FDP_PoolState& Pool = FindOrAddPool(Class);

	UObject* Instance = nullptr;

	// Pop the most-recently-returned idle instance (LIFO keeps the cache hot). Skip any that
	// went stale (e.g. a pooled actor someone destroyed out from under us).
	while (Pool.Idle.Num() > 0)
	{
		const int32 LastIdx = Pool.Idle.Num() - 1;
		TObjectPtr<UObject> Candidate = Pool.Idle[LastIdx];
		Pool.Idle.RemoveAt(LastIdx, 1);
		if (Pool.IdleSince.IsValidIndex(LastIdx))
		{
			Pool.IdleSince.RemoveAt(LastIdx, 1);
		}
		if (IsValid(Candidate))
		{
			Instance = Candidate;
			break;
		}
	}

	if (!Instance)
	{
		// Pool exhausted: grow if allowed (or if no pool config explicitly forbade it).
		if (!Pool.Config.bAllowGrow && Pool.Live.Num() > 0)
		{
			UE_LOG(LogDPPool, Verbose, TEXT("Acquire on '%s' returned null: pool exhausted and growth disallowed."),
				*Class->GetName());
			return nullptr;
		}
		Instance = CreateInstance(Class);
		if (!Instance)
		{
			UE_LOG(LogDPPool, Warning, TEXT("Acquire failed: could not create instance of '%s'."), *Class->GetName());
			return nullptr;
		}
	}

	Pool.Live.Add(Instance);
	Pool.PeakLive = FMath::Max(Pool.PeakLive, Pool.Live.Num());

	SET_DWORD_STAT(STAT_DPPoolLive, Pool.Live.Num());
	SET_DWORD_STAT(STAT_DPPoolIdle, Pool.Idle.Num());
	SET_DWORD_STAT(STAT_DPPoolPeak, Pool.PeakLive);

	NotifyAcquired(Instance);
	return Instance;
}

AActor* UDP_ObjectPoolSubsystem::AcquireActor(TSubclassOf<AActor> Class, const FTransform& Transform, AActor* Owner)
{
	if (!*Class)
	{
		UE_LOG(LogDPPool, Warning, TEXT("AcquireActor ignored: null Class."));
		return nullptr;
	}

	// Acquire() fires OnAcquiredFromPool; for actors we must activate (place/un-hide) BEFORE the
	// hook runs, so do the raw checkout here, activate, then notify — mirroring Acquire's order.
	SCOPE_CYCLE_COUNTER(STAT_DPPoolAcquire);

	FDP_PoolState& Pool = FindOrAddPool(Class);

	AActor* Actor = nullptr;
	while (Pool.Idle.Num() > 0)
	{
		const int32 LastIdx = Pool.Idle.Num() - 1;
		TObjectPtr<UObject> Candidate = Pool.Idle[LastIdx];
		Pool.Idle.RemoveAt(LastIdx, 1);
		if (Pool.IdleSince.IsValidIndex(LastIdx))
		{
			Pool.IdleSince.RemoveAt(LastIdx, 1);
		}
		if (IsValid(Candidate))
		{
			Actor = Cast<AActor>(Candidate);
			break;
		}
	}

	if (!Actor)
	{
		if (!Pool.Config.bAllowGrow && Pool.Live.Num() > 0)
		{
			UE_LOG(LogDPPool, Verbose, TEXT("AcquireActor on '%s' returned null: exhausted, growth disallowed."),
				*Class->GetName());
			return nullptr;
		}
		Actor = Cast<AActor>(CreateInstance(Class));
		if (!Actor)
		{
			UE_LOG(LogDPPool, Warning, TEXT("AcquireActor failed: could not spawn '%s'."), *Class->GetName());
			return nullptr;
		}
	}

	Pool.Live.Add(Actor);
	Pool.PeakLive = FMath::Max(Pool.PeakLive, Pool.Live.Num());

	SET_DWORD_STAT(STAT_DPPoolLive, Pool.Live.Num());
	SET_DWORD_STAT(STAT_DPPoolIdle, Pool.Idle.Num());
	SET_DWORD_STAT(STAT_DPPoolPeak, Pool.PeakLive);

	ActivateInstance(Actor, Transform, Owner);
	NotifyAcquired(Actor);
	return Actor;
}

void UDP_ObjectPoolSubsystem::Release(UObject* Instance)
{
	SCOPE_CYCLE_COUNTER(STAT_DPPoolRelease);

	if (!IsValid(Instance))
	{
		return;
	}

	FDP_PoolState* Pool = Pools.Find(Instance->GetClass());
	if (!Pool)
	{
		// Try the registered (possibly parent) pool by walking up: most pooled classes are exact,
		// but tolerate a subclass instance being released to a base-class pool.
		for (TPair<TSubclassOf<UObject>, FDP_PoolState>& Pair : Pools)
		{
			if (Pair.Value.Live.Contains(Instance))
			{
				Pool = &Pair.Value;
				break;
			}
		}
	}
	if (!Pool)
	{
		UE_LOG(LogDPPool, Warning, TEXT("Release ignored: '%s' has no pool / not checked out."),
			*GetNameSafe(Instance));
		return;
	}

	const int32 RemovedFromLive = Pool->Live.RemoveSingle(Instance);
	if (RemovedFromLive == 0)
	{
		// Double-release or never-acquired: no-op (already idle).
		return;
	}

	// Fire the user reset hook BEFORE the structural deactivation, mirroring acquire order.
	NotifyReturned(Instance);
	DeactivateInstance(Instance);

	Pool->Idle.Add(Instance);
	const double Now = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0;
	Pool->IdleSince.Add(Now);

	SET_DWORD_STAT(STAT_DPPoolLive, Pool->Live.Num());
	SET_DWORD_STAT(STAT_DPPoolIdle, Pool->Idle.Num());
}

void UDP_ObjectPoolSubsystem::ReleaseAfter(UObject* Instance, float DelaySeconds)
{
	if (!IsValid(Instance))
	{
		return;
	}
	if (DelaySeconds <= 0.f)
	{
		Release(Instance);
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		Release(Instance);
		return;
	}

	TWeakObjectPtr<UObject> WeakInstance(Instance);
	TWeakObjectPtr<UDP_ObjectPoolSubsystem> WeakSelf(this);

	// Key this pending timer by a stable id captured by value, so the one-shot lambda can drop
	// its own entry when it fires (keeping the map bounded) and Deinitialize() can cancel any
	// that are still outstanding.
	const int32 TimerId = NextReleaseTimerId++;
	FTimerHandle Handle;
	World->GetTimerManager().SetTimer(Handle, [WeakSelf, WeakInstance, TimerId]()
	{
		UDP_ObjectPoolSubsystem* Self = WeakSelf.Get();
		if (!Self)
		{
			return;
		}
		Self->PendingReleaseTimers.Remove(TimerId);
		if (UObject* Obj = WeakInstance.Get())
		{
			Self->Release(Obj);
		}
	}, DelaySeconds, false);

	PendingReleaseTimers.Add(TimerId, Handle);
}

// ============================================================================================
// Warmup
// ============================================================================================

void UDP_ObjectPoolSubsystem::WarmupAsync(TSubclassOf<UObject> Class, int32 Count, int32 InstancesPerFrame)
{
	if (!*Class || Count <= 0)
	{
		return;
	}
	if (!HasPool(Class))
	{
		RegisterPool(FDP_PoolConfig(Class));
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	TSharedPtr<FWarmupJob> Job = MakeShared<FWarmupJob>();
	Job->Class = Class;
	Job->Remaining = Count;
	Job->PerFrame = FMath::Max(1, InstancesPerFrame);
	WarmupJobs.Add(Job);

	TWeakPtr<FWarmupJob> WeakJob(Job);
	// A repeating, very-short-period timer steps the job; it clears itself when done. Using the
	// world timer (not a tickable) keeps warmup off editor/preview ticking and bound to the world.
	World->GetTimerManager().SetTimer(Job->TimerHandle, FTimerDelegate::CreateUObject(
		this, &UDP_ObjectPoolSubsystem::TickWarmup, WeakJob), 0.f, true);

	UE_LOG(LogDPPool, Verbose, TEXT("Warmup queued: %d x '%s' (%d/frame)."),
		Count, *Class->GetName(), Job->PerFrame);
}

void UDP_ObjectPoolSubsystem::TickWarmup(TWeakPtr<FWarmupJob> WeakJob)
{
	TSharedPtr<FWarmupJob> Job = WeakJob.Pin();
	if (!Job.IsValid())
	{
		return;
	}

	FDP_PoolState& Pool = FindOrAddPool(Job->Class);
	const int32 ThisFrame = FMath::Min(Job->PerFrame, Job->Remaining);
	const double Now = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0;

	for (int32 i = 0; i < ThisFrame; ++i)
	{
		if (UObject* Instance = CreateInstance(Job->Class))
		{
			Pool.Idle.Add(Instance);
			Pool.IdleSince.Add(Now);
		}
	}
	Job->Remaining -= ThisFrame;

	SET_DWORD_STAT(STAT_DPPoolIdle, Pool.Idle.Num());

	if (Job->Remaining <= 0)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(Job->TimerHandle);
		}
		const int32 IdleCount = Pool.Idle.Num();
		const TSubclassOf<UObject> JobClass = Job->Class;
		WarmupJobs.RemoveAll([&Job](const TSharedPtr<FWarmupJob>& J) { return J == Job; });

		UE_LOG(LogDPPool, Verbose, TEXT("Warmup complete for '%s' (%d idle)."), *JobClass->GetName(), IdleCount);
		OnWarmupComplete.Broadcast(JobClass, IdleCount);
	}
}

// ============================================================================================
// Eviction
// ============================================================================================

void UDP_ObjectPoolSubsystem::EnsureEvictionTimer()
{
	UWorld* World = GetWorld();
	if (!World || EvictionTimerHandle.IsValid())
	{
		return;
	}
	World->GetTimerManager().SetTimer(EvictionTimerHandle, FTimerDelegate::CreateUObject(
		this, &UDP_ObjectPoolSubsystem::EvictionSweep), GDPPoolEvictionPeriod, true);
}

void UDP_ObjectPoolSubsystem::EvictionSweep()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const double Now = World->GetRealTimeSeconds();
	int32 EvictedThisSweep = 0;

	for (TPair<TSubclassOf<UObject>, FDP_PoolState>& Pair : Pools)
	{
		FDP_PoolState& Pool = Pair.Value;
		const FDP_PoolConfig& Config = Pool.Config;
		if (Config.IdleEvictSeconds <= 0.f)
		{
			continue; // eviction disabled for this pool
		}

		// Evict from the oldest (front) end: instances beyond the soft cap that have been idle
		// longer than the threshold and aren't vetoing reclamation.
		for (int32 i = 0; i < Pool.Idle.Num(); )
		{
			const bool bOverCap = (Config.SoftCap > 0) && (Pool.Idle.Num() > Config.SoftCap);
			const double IdleAt = Pool.IdleSince.IsValidIndex(i) ? Pool.IdleSince[i] : Now;
			const bool bExpired = (Now - IdleAt) >= Config.IdleEvictSeconds;

			UObject* Instance = Pool.Idle[i];
			if (bOverCap && bExpired && IsValid(Instance) && CanReclaim(Instance))
			{
				DestroyInstance(Instance);
				Pool.Idle.RemoveAt(i, 1);
				if (Pool.IdleSince.IsValidIndex(i))
				{
					Pool.IdleSince.RemoveAt(i, 1);
				}
				++EvictedThisSweep;
			}
			else if (!IsValid(Instance))
			{
				// Stale entry: drop without counting as a deliberate eviction.
				Pool.Idle.RemoveAt(i, 1);
				if (Pool.IdleSince.IsValidIndex(i))
				{
					Pool.IdleSince.RemoveAt(i, 1);
				}
			}
			else
			{
				++i;
			}
		}

		SET_DWORD_STAT(STAT_DPPoolIdle, Pool.Idle.Num());
	}

	if (EvictedThisSweep > 0)
	{
		GDPPoolEvictedTotal += EvictedThisSweep;
		SET_DWORD_STAT(STAT_DPPoolEvicted, GDPPoolEvictedTotal);
		UE_LOG(LogDPPool, Verbose, TEXT("Evicted %d idle pooled instance(s) (%d total)."),
			EvictedThisSweep, GDPPoolEvictedTotal);
	}
}

// ============================================================================================
// Drain
// ============================================================================================

void UDP_ObjectPoolSubsystem::DrainPool(TSubclassOf<UObject> Class)
{
	FDP_PoolState* Pool = Pools.Find(Class);
	if (!Pool)
	{
		return;
	}

	// Release live instances first (fires their return hooks) so they aren't destroyed mid-use
	// without the reset contract running.
	TArray<TObjectPtr<UObject>> LiveCopy = Pool->Live;
	for (const TObjectPtr<UObject>& Obj : LiveCopy)
	{
		if (IsValid(Obj))
		{
			NotifyReturned(Obj);
		}
	}

	const int32 Destroyed = Pool->Live.Num() + Pool->Idle.Num();
	for (TObjectPtr<UObject>& Obj : Pool->Live)
	{
		DestroyInstance(Obj);
	}
	for (TObjectPtr<UObject>& Obj : Pool->Idle)
	{
		DestroyInstance(Obj);
	}

	Pools.Remove(Class);

	UE_LOG(LogDPPool, Verbose, TEXT("Drained pool '%s' (%d instance(s) destroyed)."),
		*GetNameSafe(*Class), Destroyed);
}

// ============================================================================================
// Activation / deactivation (safe structural reset for actors)
// ============================================================================================

void UDP_ObjectPoolSubsystem::ActivateInstance(UObject* Instance, const FTransform& Transform, AActor* Owner)
{
	AActor* Actor = Cast<AActor>(Instance);
	if (!Actor)
	{
		return; // plain UObjects have no transform/collision/tick to manage
	}

	Actor->SetActorTransform(Transform, false, nullptr, ETeleportType::ResetPhysics);
	Actor->SetOwner(Owner);
	Actor->SetActorHiddenInGame(false);
	Actor->SetActorEnableCollision(true);
	Actor->SetActorTickEnabled(true);

	// Wake physics on the root if simulating, so reused actors don't stay asleep at the new spot.
	if (UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Actor->GetRootComponent()))
	{
		if (Root->IsSimulatingPhysics())
		{
			Root->SetPhysicsLinearVelocity(FVector::ZeroVector);
			Root->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
			Root->WakeAllRigidBodies();
		}
	}
}

void UDP_ObjectPoolSubsystem::DeactivateInstance(UObject* Instance)
{
	AActor* Actor = Cast<AActor>(Instance);
	if (!Actor)
	{
		return;
	}

	// Zero any movement so a reused actor doesn't carry stale velocity into its next life.
	if (UMovementComponent* Movement = Actor->FindComponentByClass<UMovementComponent>())
	{
		Movement->StopMovementImmediately();
	}
	if (UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Actor->GetRootComponent()))
	{
		if (Root->IsSimulatingPhysics())
		{
			Root->SetPhysicsLinearVelocity(FVector::ZeroVector);
			Root->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
			Root->PutAllRigidBodiesToSleep();
		}
	}

	Actor->SetActorEnableCollision(false);
	Actor->SetActorHiddenInGame(true);
	Actor->SetActorTickEnabled(false);
	Actor->SetOwner(nullptr);
}

// ============================================================================================
// IDP_Poolable hook dispatch
// ============================================================================================

void UDP_ObjectPoolSubsystem::NotifyAcquired(UObject* Instance)
{
	if (Instance && Instance->Implements<UDP_Poolable>())
	{
		IDP_Poolable::Execute_OnAcquiredFromPool(Instance);
	}
}

void UDP_ObjectPoolSubsystem::NotifyReturned(UObject* Instance)
{
	if (Instance && Instance->Implements<UDP_Poolable>())
	{
		IDP_Poolable::Execute_OnReturnedToPool(Instance);
	}
}

bool UDP_ObjectPoolSubsystem::CanReclaim(const UObject* Instance)
{
	if (Instance && Instance->Implements<UDP_Poolable>())
	{
		// Execute_ needs a mutable UObject*; the hook is logically const (CanBeReclaimed const).
		return IDP_Poolable::Execute_CanBeReclaimed(const_cast<UObject*>(Instance));
	}
	return true;
}

// ============================================================================================
// Destruction
// ============================================================================================

void UDP_ObjectPoolSubsystem::DestroyInstance(UObject* Instance)
{
	if (!IsValid(Instance))
	{
		return;
	}
	if (AActor* Actor = Cast<AActor>(Instance))
	{
		Actor->Destroy();
	}
	else
	{
		Instance->MarkAsGarbage();
	}
}

// ============================================================================================
// Queries / debug
// ============================================================================================

int32 UDP_ObjectPoolSubsystem::GetLiveCount(TSubclassOf<UObject> Class) const
{
	const FDP_PoolState* Pool = Pools.Find(Class);
	return Pool ? Pool->Live.Num() : 0;
}

int32 UDP_ObjectPoolSubsystem::GetIdleCount(TSubclassOf<UObject> Class) const
{
	const FDP_PoolState* Pool = Pools.Find(Class);
	return Pool ? Pool->Idle.Num() : 0;
}

FString UDP_ObjectPoolSubsystem::GetDPDebugString_Implementation() const
{
	int32 TotalLive = 0;
	int32 TotalIdle = 0;
	int32 TotalPeak = 0;
	for (const TPair<TSubclassOf<UObject>, FDP_PoolState>& Pair : Pools)
	{
		TotalLive += Pair.Value.Live.Num();
		TotalIdle += Pair.Value.Idle.Num();
		TotalPeak += Pair.Value.PeakLive;
	}
	return FString::Printf(
		TEXT("ObjectPool: %d pool(s)  live=%d idle=%d peak=%d evicted=%d"),
		Pools.Num(), TotalLive, TotalIdle, TotalPeak, GDPPoolEvictedTotal);
}
