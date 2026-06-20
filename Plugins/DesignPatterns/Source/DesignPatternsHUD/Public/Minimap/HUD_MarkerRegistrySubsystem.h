// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "UObject/WeakInterfacePtr.h"
#include "HUD_MarkerRegistrySubsystem.generated.h"

class IHUD_Trackable;

/** Broadcast when the set of registered trackables changes (registration / unregistration). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FHUD_OnMarkerSetChanged);

/**
 * World-scoped registry of every IHUD_Trackable currently present in the world.
 *
 * This is the single decoupling point between gameplay (actors/components implementing IHUD_Trackable)
 * and the cosmetic minimap projection (UHUD_MinimapViewModel). Trackables register themselves on
 * BeginPlay and unregister on EndPlay; the registry holds them as a WEAK interface set so a destroyed
 * actor's entry simply goes stale and is pruned — the registry never extends a trackable's lifetime and
 * never leaks a dead world's objects.
 *
 * The registry is purely a read model: it performs no projection itself. The ViewModel snapshots the live
 * set each refresh. It publishes itself into the service locator (HUDTags::Service_MarkerRegistry,
 * WeakObserved) so the ViewModel can resolve it by stable tag without a world-subsystem round-trip, and
 * is itself resolvable as a world subsystem via FDP_SubsystemStatics::GetWorldSubsystem.
 *
 * Entirely local/cosmetic: it is driven by already-replicated gameplay (actors exist on each client) and
 * is never replicated.
 */
UCLASS()
class DESIGNPATTERNSHUD_API UHUD_MarkerRegistrySubsystem : public UDP_WorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	//~ Begin UDP_WorldSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_WorldSubsystem

	/** Fired after a trackable is registered or unregistered (so a ViewModel can refresh eagerly). */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|HUD|Minimap")
	FHUD_OnMarkerSetChanged OnMarkerSetChanged;

	/**
	 * Register a trackable. Idempotent: registering the same object twice is a no-op. The registry holds
	 * only a weak interface ref, so the caller remains the sole owner. Null / non-implementing objects are
	 * rejected (logged in non-shipping).
	 */
	void RegisterTrackable(TScriptInterface<IHUD_Trackable> Trackable);

	/** Remove a previously-registered trackable. Safe to call for an unregistered/null object (no-op). */
	void UnregisterTrackable(TScriptInterface<IHUD_Trackable> Trackable);

	/**
	 * Snapshot the currently-live trackables into OutTrackables (pruning stale weak entries as a side
	 * effect). Cleared then filled. The ViewModel calls this each projection refresh.
	 */
	void GetLiveTrackables(TArray<TScriptInterface<IHUD_Trackable>>& OutTrackables) const;

	/** Number of currently-live (post-prune) registered trackables. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|HUD|Minimap")
	int32 GetTrackableCount() const;

private:
	/** Local authority helper (UWorldSubsystem has none): true on listen-server/standalone, false on a pure client. */
	bool HasWorldAuthority() const { const UWorld* W = GetWorld(); return W && W->GetNetMode() != NM_Client; }

	/** Drop any weak entries whose object has been GC'd. Const: mutates only the transient weak set. */
	void PruneStale() const;

	/** Publish/withdraw this registry from the service locator under HUDTags::Service_MarkerRegistry. */
	void PublishToLocator(bool bRegister);

	/**
	 * The weak trackable set. WeakInterfacePtr is non-owning and null-checked on every read; a destroyed
	 * trackable simply becomes invalid and is pruned. Mutable so const reads (snapshot/debug) can prune.
	 */
	mutable TArray<TWeakInterfacePtr<IHUD_Trackable>> Trackables;
};
