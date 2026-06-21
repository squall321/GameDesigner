// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "Engine/TimerHandle.h"
#include "Vfx/Seam_VfxController.h"
#include "WS_VfxManagerSubsystem.generated.h"

class UWS_VfxBankDataAsset;
struct FWS_VfxEntry;
class UFXSystemAsset;
class UFXSystemComponent;
class AWS_VfxCarrier;

/**
 * One tracked, recyclable VFX instance (attached, looping, or a one-shot awaiting completion).
 *
 * Declared at file scope (UHT does not allow USTRUCTs nested inside a UCLASS) but is a private
 * implementation detail of UWS_VfxManagerSubsystem — not part of the public Blueprint API. The carrier
 * is referenced weakly because the core object pool (or the world, for non-pooled) owns the actor; this
 * manager only tracks it for StopVfx and never extends its lifetime.
 */
struct FWS_TrackedVfx
{
	/** Stable id mirrored into the FSeam_VfxHandle handed out. */
	int64 HandleId = 0;

	/** The pooled carrier actor hosting this effect's component. Non-owning (the pool/world owns it). */
	TWeakObjectPtr<AWS_VfxCarrier> Carrier;

	/** The effect tag, for debug/auditing. */
	FGameplayTag VfxTag;

	/** True if this entry loops (must be StopVfx'd; never auto-reclaimed on the one-shot timer). */
	bool bLooping = false;

	/** Whether this effect's carrier came from the pool (vs a plain transient spawn). */
	bool bPooled = false;

	/** World real-time seconds at spawn, for the one-shot reclaim safety net. */
	double SpawnRealTime = 0.0;
};

/**
 * GameInstance-scoped, tag-keyed cosmetic VFX manager.
 *
 * Implements the shared ISeam_VfxController seam: any system (weather, HUD, interaction, narrative,
 * camera) requests a particle effect BY TAG and never depends on this concrete class or on the Niagara
 * module. Effects are resolved from data-driven VFX banks (UWS_VfxBankDataAsset) into engine
 * UFXSystemAsset references (the base of both Cascade and Niagara), spawned through the engine spawn
 * helpers, and carried by a tiny pooled actor (AWS_VfxCarrier) recycled through the core
 * UDP_ObjectPoolSubsystem so high-frequency one-shots cost no per-spawn allocation.
 *
 * VFX is purely LOCAL and COSMETIC — driven by already-replicated gameplay (OnReps / the message bus),
 * and is NEVER replicated. On a dedicated server / -nullrhi there is no rendering, so every spawn path
 * becomes a guarded no-op while the handle bookkeeping still behaves (callers get a valid-looking but
 * inert handle, and StopVfx is safe).
 *
 * The manager registers itself under DP.Service.Vfx so consumers resolve it via the service locator.
 * Looping/attached effects are tracked by handle so StopVfx can stop and recycle their carrier; one-shots
 * are released automatically on completion (with a safety-net timeout from settings).
 */
UCLASS()
class DESIGNPATTERNSWORLDSYSTEMS_API UWS_VfxManagerSubsystem : public UDP_GameInstanceSubsystem, public ISeam_VfxController
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	// ---- ISeam_VfxController ----

	/** Spawn a one-shot VFX (by tag) at a world location/rotation. Returns a tracked handle for looping entries, else invalid. */
	virtual FSeam_VfxHandle SpawnVfxAtLocation_Implementation(FGameplayTag VfxTag, FVector Location, FRotator Rotation) override;

	/** Spawn a VFX attached to a component/socket. Returns a handle so it can be stopped. */
	virtual FSeam_VfxHandle SpawnVfxAttached_Implementation(FGameplayTag VfxTag, USceneComponent* AttachTo, FName Socket) override;

	/** Stop a previously-spawned attached/looping VFX and recycle its carrier. */
	virtual void StopVfx_Implementation(FSeam_VfxHandle Handle) override;

	// ---- Bank management ----

	/**
	 * Register a VFX bank's entries into the manager's flat tag->entry index. First registration of a tag
	 * wins (duplicates across banks are logged and ignored). Optionally pre-warms pooled entries.
	 */
	UFUNCTION(BlueprintCallable, Category = "WorldSystems|Vfx")
	void RegisterBank(UWS_VfxBankDataAsset* Bank);

	/** True if any registered bank provides VfxTag. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "WorldSystems|Vfx")
	bool HasVfx(FGameplayTag VfxTag) const;

	/** Number of currently-tracked (attached/looping) effects retained for StopVfx. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "WorldSystems|Vfx")
	int32 GetTrackedCount() const { return TrackedEffects.Num(); }

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

private:
	// ---- Setup ----

	/** Register the VFX banks named in the developer settings. */
	void RegisterDefaultBanksFromSettings();

	/** Register this manager under DP.Service.Vfx (strong-owned: GI lifetime matches the seam contract). */
	void RegisterAsService();

	/** Remove our service registration. */
	void UnregisterAsService();

	// ---- Resolution / spawn core ----

	/** Find the effect entry for VfxTag across all registered banks, or null. */
	const FWS_VfxEntry* FindEntry(FGameplayTag VfxTag) const;

	/** Synchronously resolve the entry's UFXSystemAsset (soft-load on first use), or null. */
	UFXSystemAsset* LoadSystem(const FWS_VfxEntry& Entry) const;

	/**
	 * Acquire a carrier actor for an effect: from the core pool if the entry is pooled (warming the pool
	 * lazily), otherwise a plain transient spawn. Returns null if no world / spawn fails.
	 */
	AWS_VfxCarrier* AcquireCarrier(const FWS_VfxEntry& Entry);

	/** Return a carrier to its pool (pooled) or destroy it (non-pooled). Safe with a null/garbage carrier. */
	void ReleaseCarrier(AWS_VfxCarrier* CarrierActor, bool bPooled);

	/**
	 * Common spawn path. Resolves + loads the system, acquires a carrier, activates its FX component with
	 * the system, and either tracks it (looping/attached) or schedules one-shot auto-release. Returns the
	 * handle (invalid for an untracked one-shot at location).
	 */
	FSeam_VfxHandle SpawnInternal(FGameplayTag VfxTag, const FTransform& WorldTransform,
		USceneComponent* AttachTo, FName Socket, bool bAttached);

	/** Allocate the next monotonic handle id (never 0, so an invalid handle stays distinguishable). */
	int64 AllocateHandleId();

	/** Find the tracked entry for a handle id, or null. */
	FWS_TrackedVfx* FindTracked(int64 HandleId);

	/** Release and untrack a single tracked effect (recycles its carrier). */
	void ReleaseTracked(int64 HandleId);

	/** Enforce MaxTrackedVfx by reclaiming the oldest tracked one-shot/effect when over budget. */
	void EnforceTrackedBudget();

	/** Timer callback: reclaim any one-shot whose carrier outlived OneShotReclaimSeconds (safety net). */
	void SweepStaleOneShots();

	/** Ensure the recurring stale-one-shot sweep timer is running. */
	void EnsureSweepTimer();

	// ---- State ----

	/** Flat tag -> entry index built from every registered bank. Points into BankRefs' Entries arrays. */
	TMap<FGameplayTag, const FWS_VfxEntry*> EntryIndex;

	/** Strong refs to registered banks so EntryIndex's pointers stay valid (the banks own the entries). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UWS_VfxBankDataAsset>> RegisteredBanks;

	/**
	 * All currently tracked (attached/looping/pending) effects, keyed by handle id. Not a UPROPERTY: the
	 * carriers are owned by the pool/world (referenced weakly here), so the manager roots nothing.
	 */
	TMap<int64, FWS_TrackedVfx> TrackedEffects;

	/** Monotonic handle id source (starts at 1; 0 is reserved for an invalid handle). */
	int64 NextHandleId = 1;

	/** Recurring timer that sweeps stale one-shot carriers as a safety net. */
	FTimerHandle SweepTimerHandle;

	/**
	 * True when rendering is available (false on dedicated server / -nullrhi). When false every spawn is a
	 * guarded no-op that still returns a coherent handle so callers/StopVfx behave.
	 */
	bool bFxAvailable = false;
};
