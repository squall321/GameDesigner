// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "Services/DPServiceTypes.h"
#include "GameplayTagContainer.h"
#include "DPServiceLocatorSubsystem.generated.h"

/**
 * GameplayTag-keyed Service Locator / lightweight dependency-injection registry.
 *
 * WHEN TO USE THIS (and when not to):
 *   Engine-native resolution comes FIRST. For engine-owned objects, resolve them the
 *   normal way — UGameInstance::GetSubsystem, UWorld::GetSubsystem,
 *   FDP_SubsystemStatics::GetGameInstanceSubsystem<T>, GetWorld()->GetGameState(), etc.
 *   Reach for this locator only for game-authored, cross-cutting providers you want to
 *   look up by a STABLE TAG instead of a concrete class — e.g. an interface-typed
 *   "analytics" service, a "save backend", or a manager actor — and where the consumer
 *   should not hard-depend on the provider's concrete type.
 *
 * KEYING:
 *   Services are keyed by FGameplayTag (NOT by TSubclassOf<UInterface>). Tags are stable,
 *   designer-visible, hierarchy-aware and decouple the consumer from the provider class.
 *   Anchor your keys under DPNativeTags::Service in the project's tag table.
 *
 * LIFETIME:
 *   - StrongOwned : the locator keeps the provider alive (UPROPERTY). Remove explicitly.
 *   - WeakObserved: the locator observes only; if the provider is GC'd the entry
 *                   auto-invalidates and OnServiceInvalidated fires. Resolving a dangling
 *                   weak provider ensure()s in non-shipping builds to surface the bug.
 *
 * SCOPE: GameInstance-scoped, so registrations survive level travel. Register
 *   world-lifetime providers as WeakObserved so they cannot leak a dead world's objects.
 */
UCLASS()
class DESIGNPATTERNS_API UDP_ServiceLocatorSubsystem : public UDP_GameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/** Fires after a provider is registered (including a successful override). */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Services")
	FDP_OnServiceRegistered OnServiceRegistered;

	/** Fires when a key's binding is removed, overwritten, or auto-invalidated (weak GC). */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Services")
	FDP_OnServiceInvalidated OnServiceInvalidated;

	/**
	 * Register Provider under Key with the given ownership lifetime.
	 *
	 * Double-registration is validated: if Key is already bound to a LIVE provider and
	 * bAllowOverride is false, the call is rejected (logged) and returns false. With
	 * bAllowOverride true, the previous binding is invalidated (OnServiceInvalidated)
	 * and replaced. A stale (GC'd weak) binding is always silently replaceable.
	 *
	 * @param Key            Stable service tag (must be valid). Anchor under DP.Service.
	 * @param Provider       The provider object (must be non-null).
	 * @param Lifetime       StrongOwned (locator keeps alive) or WeakObserved (observe only).
	 * @param bAllowOverride Permit replacing an existing live binding.
	 * @return True on success.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Services",
		meta = (AdvancedDisplay = "bAllowOverride"))
	bool RegisterService(FGameplayTag Key, UObject* Provider, EDP_ServiceLifetime Lifetime, bool bAllowOverride = false);

	/**
	 * Resolve the live provider for Key, or null if unbound / stale.
	 *
	 * For a WeakObserved entry whose provider has been GC'd this ensure()s in non-shipping
	 * builds (the registrant failed to keep its provider alive or to unregister) and lazily
	 * invalidates the slot.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Services")
	UObject* ResolveService(FGameplayTag Key) const;

	/** Typed convenience resolve: returns the provider cast to T, or null. */
	template <typename T>
	T* Resolve(FGameplayTag Key) const
	{
		return Cast<T>(ResolveService(Key));
	}

	/** True if Key is currently bound to a live provider. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Services")
	bool IsRegistered(FGameplayTag Key) const;

	/**
	 * Remove the binding for Key (if any) and broadcast OnServiceInvalidated.
	 * For StrongOwned this drops the locator's strong reference, allowing GC.
	 * @return True if a binding existed and was removed.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Services")
	bool UnregisterService(FGameplayTag Key);

	/** Number of currently-bound (live or not-yet-pruned) service keys. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Services")
	int32 GetServiceCount() const;

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

	/**
	 * Log every registered key with its lifetime and current provider — backing for the
	 * DP.Service.List console command. Prunes stale weak entries as a side effect.
	 */
	void DumpServices() const;

private:
	/** Key -> entry. One slot per tag; overriding replaces the slot in place. */
	UPROPERTY()
	TMap<FGameplayTag, FDP_ServiceEntry> Services;

	/**
	 * Drop any WeakObserved entries whose provider has been GC'd, broadcasting
	 * OnServiceInvalidated for each. Const because it mutates only mutable transient
	 * bookkeeping; callable from const resolve/dump paths.
	 */
	void PruneStale() const;
};
