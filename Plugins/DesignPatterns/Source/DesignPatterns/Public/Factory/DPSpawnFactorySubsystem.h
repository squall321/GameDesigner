// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "Factory/DPSpawnRecipe.h"
#include "DPSpawnFactorySubsystem.generated.h"

class AActor;
class UDP_SpawnFactory;

/**
 * World-scoped registry + dispatcher for spawn factories.
 *
 * Maps an FGameplayTag identity to a factory *class* (UDP_SpawnFactory subclass). On Spawn,
 * it resolves the factory CDO and calls CreateActor. Optionally — and softly — it routes the
 * request through the object pool subsystem (UDP_ObjectPoolSubsystem) when that system is
 * present in the world and the call permits pooling; the pool is resolved by name so this
 * module does NOT hard-depend on the pool system.
 *
 * Why a World subsystem: spawning is world-bound and registrations should die with the world.
 * Why register classes (not instances): factories are stateless and used via their CDO, so we
 * avoid keeping live factory objects around and keep registration trivially GC-safe.
 */
UCLASS()
class DESIGNPATTERNS_API UDP_SpawnFactorySubsystem : public UDP_WorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/**
	 * Register a factory class under an identity tag. Re-registering a tag replaces the prior
	 * class and logs a warning. Returns false if the tag is invalid or the class is null.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Factory")
	bool RegisterFactory(FGameplayTag IdentityTag, TSubclassOf<UDP_SpawnFactory> FactoryClass);

	/** Remove a factory registration. Returns true if a registration was removed. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Factory")
	bool UnregisterFactory(FGameplayTag IdentityTag);

	/** True if a factory is registered for the given identity tag (exact match). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Factory")
	bool IsFactoryRegistered(FGameplayTag IdentityTag) const;

	/**
	 * Spawn (or fetch from pool) an actor for the identity carried by Params.IdentityTag.
	 * If Params.IdentityTag is unset it falls back to the supplied IdentityTag argument.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Factory")
	AActor* Spawn(FGameplayTag IdentityTag, const FDP_SpawnParams& Params);

	/** Convenience: spawn using only an identity tag and a transform. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Factory")
	AActor* SpawnAt(FGameplayTag IdentityTag, const FTransform& Transform);

	/** Every identity tag that currently has a registered factory. Backing for DP.Factory.ListRegistered. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Factory")
	TArray<FGameplayTag> ListRegistered() const;

	/** Number of registered factories. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Factory")
	int32 GetRegisteredCount() const { return Factories.Num(); }

	/** Dump the registry (tag -> factory class) to the log — backing for DP.Factory.ListRegistered. */
	void DumpRegistry() const;

	//~ Begin UDP_WorldSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_WorldSubsystem

private:
	/** Identity tag -> factory class. Classes are GC-rooted via the UPROPERTY map below. */
	UPROPERTY(Transient)
	TMap<FGameplayTag, TSubclassOf<UDP_SpawnFactory>> Factories;

	/** Get (lazily creating) the CDO-style factory instance for a class. */
	UDP_SpawnFactory* GetFactoryInstance(TSubclassOf<UDP_SpawnFactory> FactoryClass);

	/**
	 * Best-effort, soft-coupled attempt to fetch an actor from the object pool subsystem.
	 * Returns nullptr if the pool subsystem is absent, the class is unpoolable, or pooling
	 * was declined. Never hard-references the pool module's symbols.
	 */
	AActor* TryAcquireFromPool(TSubclassOf<AActor> Class, const FDP_SpawnParams& Params);
};
