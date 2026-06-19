// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "DPPoolable.generated.h"

/**
 * UInterface boilerplate for IDP_Poolable. Mark any pooled UObject/AActor class with this
 * interface (in C++ or Blueprint) to receive lifecycle callbacks from UDP_ObjectPoolSubsystem.
 */
UINTERFACE(BlueprintType, MinimalAPI, meta = (DisplayName = "DP Poolable"))
class UDP_Poolable : public UInterface
{
	GENERATED_BODY()
};

/**
 * Lifecycle hooks for pooled objects.
 *
 * The object pool reuses instances instead of destroying/respawning them, so an instance's
 * gameplay state is NOT reset for you beyond the safe default (transform, velocity, collision,
 * visibility and tick — see UDP_ObjectPoolSubsystem). Any latent state the instance owns —
 * active timers, Gameplay Abilities, montages, Niagara components, AI logic, delegate bindings,
 * dynamic materials, replicated counters — MUST be torn down/reset by the instance itself in
 * these hooks. Treat OnReturnedToPool like an actor's EndPlay and OnAcquiredFromPool like
 * BeginPlay: if you start it on acquire, stop it on return.
 *
 * All three are BlueprintNativeevent so designers can author the reset in Blueprint while C++
 * classes override the *_Implementation. Hooks are invoked by the subsystem on the game thread.
 */
class DESIGNPATTERNS_API IDP_Poolable
{
	GENERATED_BODY()

public:
	/**
	 * Called immediately after the instance is handed out of the pool (after the subsystem's
	 * safe default activation for actors). Re-arm whatever the instance needs to "go live":
	 * restart timers/abilities, rebind delegates, randomize, reset health, etc.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Pool")
	void OnAcquiredFromPool();
	virtual void OnAcquiredFromPool_Implementation() {}

	/**
	 * Called when the instance is returned to the pool, before the subsystem deactivates it.
	 * Tear down ALL latent state here (mirror of OnAcquiredFromPool) so the next acquirer gets
	 * a clean instance. Failing to do this is the classic object-pool bug — leftover timers or
	 * abilities firing on a "dead" pooled object.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Pool")
	void OnReturnedToPool();
	virtual void OnReturnedToPool_Implementation() {}

	/**
	 * Asked before the pool reclaims an idle instance (e.g. during eviction). Return false to
	 * veto reclamation while the instance is still doing meaningful work it can't safely abort
	 * (e.g. mid-explosion VFX). Defaults to true. Const-correct: must not mutate the instance.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Pool")
	bool CanBeReclaimed() const;
	virtual bool CanBeReclaimed_Implementation() const { return true; }
};
