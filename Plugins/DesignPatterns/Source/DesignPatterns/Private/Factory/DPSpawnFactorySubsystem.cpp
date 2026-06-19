// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Factory/DPSpawnFactorySubsystem.h"
#include "Factory/DPSpawnFactory.h"
#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

// Soft, optional dependency on the object pool. It lives in this same module, so a direct
// include is type-safe; the coupling stays *soft* because we resolve the subsystem at runtime
// via FDP_SubsystemStatics and tolerate its absence (e.g. pooling disabled / class not pooled).
#if __has_include("Pool/DPObjectPoolSubsystem.h")
#include "Pool/DPObjectPoolSubsystem.h"
#define DP_HAS_OBJECT_POOL 1
#else
#define DP_HAS_OBJECT_POOL 0
#endif

DECLARE_CYCLE_STAT(TEXT("Factory Subsystem Spawn"), STAT_DP_FactorySubsystemSpawn, STATGROUP_DesignPatterns);
DECLARE_DWORD_COUNTER_STAT(TEXT("Factory Pool Hits"), STAT_DP_FactoryPoolHits, STATGROUP_DesignPatterns);

void UDP_SpawnFactorySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogDPFactory, Log, TEXT("UDP_SpawnFactorySubsystem initialized for world '%s'."),
		GetWorld() ? *GetWorld()->GetName() : TEXT("<none>"));
}

void UDP_SpawnFactorySubsystem::Deinitialize()
{
	UE_LOG(LogDPFactory, Log, TEXT("UDP_SpawnFactorySubsystem deinitialized (%d factories)."), Factories.Num());
	Factories.Reset();
	Super::Deinitialize();
}

bool UDP_SpawnFactorySubsystem::RegisterFactory(FGameplayTag IdentityTag, TSubclassOf<UDP_SpawnFactory> FactoryClass)
{
	if (!IdentityTag.IsValid())
	{
		UE_LOG(LogDPFactory, Warning, TEXT("RegisterFactory: invalid identity tag."));
		return false;
	}
	if (!FactoryClass)
	{
		UE_LOG(LogDPFactory, Warning, TEXT("RegisterFactory: null factory class for tag '%s'."), *IdentityTag.ToString());
		return false;
	}
	if (FactoryClass->HasAnyClassFlags(CLASS_Abstract))
	{
		UE_LOG(LogDPFactory, Warning, TEXT("RegisterFactory: factory class '%s' is abstract (tag '%s')."),
			*FactoryClass->GetName(), *IdentityTag.ToString());
		return false;
	}

	if (const TSubclassOf<UDP_SpawnFactory>* Existing = Factories.Find(IdentityTag))
	{
		UE_LOG(LogDPFactory, Warning, TEXT("RegisterFactory: replacing factory for tag '%s' ('%s' -> '%s')."),
			*IdentityTag.ToString(),
			*GetNameSafe(Existing->Get()),
			*FactoryClass->GetName());
	}

	Factories.Add(IdentityTag, FactoryClass);
	UE_LOG(LogDPFactory, Verbose, TEXT("Registered factory '%s' for tag '%s'."),
		*FactoryClass->GetName(), *IdentityTag.ToString());
	return true;
}

bool UDP_SpawnFactorySubsystem::UnregisterFactory(FGameplayTag IdentityTag)
{
	const int32 Removed = Factories.Remove(IdentityTag);
	if (Removed > 0)
	{
		UE_LOG(LogDPFactory, Verbose, TEXT("Unregistered factory for tag '%s'."), *IdentityTag.ToString());
	}
	return Removed > 0;
}

bool UDP_SpawnFactorySubsystem::IsFactoryRegistered(FGameplayTag IdentityTag) const
{
	return Factories.Contains(IdentityTag);
}

UDP_SpawnFactory* UDP_SpawnFactorySubsystem::GetFactoryInstance(TSubclassOf<UDP_SpawnFactory> FactoryClass)
{
	if (!FactoryClass)
	{
		return nullptr;
	}
	// Factories are stateless and CDO-friendly; the class default object is a safe, GC-rooted
	// instance to call CreateActor on without allocating per-spawn.
	return FactoryClass->GetDefaultObject<UDP_SpawnFactory>();
}

AActor* UDP_SpawnFactorySubsystem::Spawn(FGameplayTag IdentityTag, const FDP_SpawnParams& Params)
{
	SCOPE_CYCLE_COUNTER(STAT_DP_FactorySubsystemSpawn);

	// Resolve the effective identity: explicit argument wins, else the one carried in Params.
	FGameplayTag EffectiveTag = IdentityTag.IsValid() ? IdentityTag : Params.IdentityTag;
	if (!EffectiveTag.IsValid())
	{
		UE_LOG(LogDPFactory, Warning, TEXT("Spawn: no valid identity tag supplied."));
		return nullptr;
	}

	const TSubclassOf<UDP_SpawnFactory>* FactoryClassPtr = Factories.Find(EffectiveTag);
	if (!FactoryClassPtr || !*FactoryClassPtr)
	{
		UE_LOG(LogDPFactory, Warning, TEXT("Spawn: no factory registered for tag '%s'."), *EffectiveTag.ToString());
		return nullptr;
	}

	UDP_SpawnFactory* Factory = GetFactoryInstance(*FactoryClassPtr);
	if (!Factory)
	{
		UE_LOG(LogDPFactory, Error, TEXT("Spawn: could not obtain factory instance for tag '%s'."), *EffectiveTag.ToString());
		return nullptr;
	}

	// Normalize params so the identity tag is always present for downstream hooks.
	FDP_SpawnParams EffectiveParams = Params;
	EffectiveParams.IdentityTag = EffectiveTag;

	// Optional pool route: only if permitted and the factory can name a concrete class.
	if (EffectiveParams.bAllowPooling)
	{
		const TSubclassOf<AActor> ActorClass = Factory->ResolveActorClassForParams(EffectiveParams);
		if (ActorClass)
		{
			if (AActor* Pooled = TryAcquireFromPool(ActorClass, EffectiveParams))
			{
				INC_DWORD_STAT(STAT_DP_FactoryPoolHits);
				Factory->OnActorReused(Pooled, EffectiveParams);
				UE_LOG(LogDPFactory, Verbose, TEXT("Spawn: served '%s' from pool for tag '%s'."),
					*Pooled->GetName(), *EffectiveTag.ToString());
				return Pooled;
			}
		}
	}

	return Factory->CreateActor(this, EffectiveParams);
}

AActor* UDP_SpawnFactorySubsystem::SpawnAt(FGameplayTag IdentityTag, const FTransform& Transform)
{
	FDP_SpawnParams Params;
	Params.IdentityTag = IdentityTag;
	Params.Transform = Transform;
	return Spawn(IdentityTag, Params);
}

AActor* UDP_SpawnFactorySubsystem::TryAcquireFromPool(TSubclassOf<AActor> Class, const FDP_SpawnParams& Params)
{
	if (!Class)
	{
		return nullptr;
	}

#if DP_HAS_OBJECT_POOL
	// Resolve the pool subsystem softly: if no pool exists in this world (e.g. the feature is
	// off, or this class was never pooled) AcquireActor returns null and we fall back to spawn.
	UDP_ObjectPoolSubsystem* Pool = FDP_SubsystemStatics::GetWorldSubsystem<UDP_ObjectPoolSubsystem>(this);
	if (!Pool)
	{
		return nullptr;
	}

	AActor* Pooled = Pool->AcquireActor(Class, Params.Transform, Params.Owner.Get());
	return Pooled;
#else
	(void)Params;
	return nullptr;
#endif
}

TArray<FGameplayTag> UDP_SpawnFactorySubsystem::ListRegistered() const
{
	TArray<FGameplayTag> Out;
	Factories.GenerateKeyArray(Out);
	return Out;
}

void UDP_SpawnFactorySubsystem::DumpRegistry() const
{
	UE_LOG(LogDPFactory, Log, TEXT("=== Spawn Factory Registry (%d) ==="), Factories.Num());
	for (const TPair<FGameplayTag, TSubclassOf<UDP_SpawnFactory>>& Pair : Factories)
	{
		UE_LOG(LogDPFactory, Log, TEXT("  %s -> %s"),
			*Pair.Key.ToString(), *GetNameSafe(Pair.Value.Get()));
	}
}

FString UDP_SpawnFactorySubsystem::GetDPDebugString_Implementation() const
{
	return FString::Printf(TEXT("SpawnFactory: %d registered"), Factories.Num());
}
