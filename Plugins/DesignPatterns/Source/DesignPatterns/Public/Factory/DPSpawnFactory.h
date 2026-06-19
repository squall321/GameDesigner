// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Factory/DPSpawnRecipe.h"
#include "DPSpawnFactory.generated.h"

class AActor;
class UWorld;

/**
 * Abstract, Blueprintable factory for turning an FDP_SpawnParams into a live AActor.
 *
 * This is the classic GoF Factory Method, UObject-style. The subsystem keeps factory *classes*
 * (not instances) keyed by GameplayTag and uses each factory's CDO to create actors — so a
 * factory must be stateless / CDO-friendly: do not store per-spawn state on the factory object.
 *
 * Subclass in C++ or Blueprint and override CreateActor (a BlueprintNativeEvent) to customise
 * how the actor is constructed, initialised, and finished. The default C++ implementation
 * performs a deferred SpawnActor + FinishSpawning so subclasses can mutate the actor before
 * BeginPlay via the OnActorPreFinish hook.
 */
UCLASS(Abstract, Blueprintable, BlueprintType, EditInlineNew)
class DESIGNPATTERNS_API UDP_SpawnFactory : public UObject
{
	GENERATED_BODY()

public:
	UDP_SpawnFactory();

	/**
	 * Create (or fetch) an actor for the given params. Designer-overridable.
	 *
	 * @param WorldContext  Any object resolvable to the target UWorld.
	 * @param Params        Where/who/how to spawn, plus the resolved identity.
	 * @return The spawned actor, or nullptr on failure.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "DesignPatterns|Factory")
	AActor* CreateActor(UObject* WorldContext, const FDP_SpawnParams& Params);
	virtual AActor* CreateActor_Implementation(UObject* WorldContext, const FDP_SpawnParams& Params);

	/**
	 * The actor class this factory will spawn for the given params. The default implementation
	 * returns ExplicitActorClass when set; subclasses can pick a class dynamically (e.g. by tier).
	 * The subsystem uses this to decide whether the object pool can service the request.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "DesignPatterns|Factory")
	TSubclassOf<AActor> ResolveActorClassForParams(const FDP_SpawnParams& Params) const;
	virtual TSubclassOf<AActor> ResolveActorClassForParams_Implementation(const FDP_SpawnParams& Params) const;

	/**
	 * Hook called after the actor is constructed (deferred) but before FinishSpawning, i.e.
	 * before BeginPlay. Override to inject data from Params onto the actor. Designer-overridable.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "DesignPatterns|Factory")
	void OnActorPreFinish(AActor* Actor, const FDP_SpawnParams& Params);
	virtual void OnActorPreFinish_Implementation(AActor* Actor, const FDP_SpawnParams& Params);

	/**
	 * Hook called when an actor that was *reused from a pool* is handed out (no SpawnActor ran).
	 * Override to re-arm a recycled actor. Designer-overridable. Default does nothing.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "DesignPatterns|Factory")
	void OnActorReused(AActor* Actor, const FDP_SpawnParams& Params);
	virtual void OnActorReused_Implementation(AActor* Actor, const FDP_SpawnParams& Params);

protected:
	/**
	 * Optional fixed class to spawn. Leave null and override ResolveActorClassForParams for
	 * dynamic selection. EditDefaultsOnly because factories are used via their CDO.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DesignPatterns|Factory", meta = (AllowAbstract = "false"))
	TSoftClassPtr<AActor> ExplicitActorClass;

	/** Shared deferred-spawn helper used by the default CreateActor implementation. */
	AActor* SpawnDeferredAndFinish(UWorld* World, TSubclassOf<AActor> Class, const FDP_SpawnParams& Params);
};
