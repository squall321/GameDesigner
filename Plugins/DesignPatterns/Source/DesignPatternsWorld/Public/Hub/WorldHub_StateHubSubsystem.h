// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "Persist/Seam_Persistable.h"
#include "Query/WorldHub_Queryable.h"
#include "Query/WorldHub_StateProvider.h"
#include "Hub/WorldHub_Scope.h"
#include "Registry/WorldHub_FlagTypes.h"
#include "Registry/WorldHub_FlagRegistry.h"
#include "Net/Seam_NetValue.h"
#include "GameplayTagContainer.h"
#include "UObject/WeakInterfacePtr.h"

// FInstancedStruct lives in StructUtils on UE 5.3/5.4, merged into CoreUObject in 5.5.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "WorldHub_StateHubSubsystem.generated.h"

class UWorldHub_FlagRegistry;
class UWorldHub_ScopedBlackboard;
class UWorldHub_FlagSetDataAsset;
class UWorldHub_StateRepComponent;
class UWorldHub_GameStateHubSubsystem;

/**
 * A flat snapshot of the hub's replicable state, used to push the full net-state from the
 * replication carrier into the local hub in one call (the Survival "SyncFromServer" pattern).
 *
 * Deliberately a tiny USTRUCT carrying only flat (Scope, Key, FSeam_NetValue) rows — no UObject
 * refs, no weak pointers, no FInstancedStruct — so it is safe to build on the carrier and hand to
 * the hub, and safe to flatten into a bus payload.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSWORLD_API FWorldHub_RepState
{
	GENERATED_BODY()

	/** The full set of replicated (Scope, Key, Value) entries the client should mirror. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|WorldHub")
	TArray<FWorldHub_ScopedRepEntry> Entries;

	FWorldHub_RepState() = default;
	explicit FWorldHub_RepState(const TArray<FWorldHub_ScopedRepEntry>& InEntries) : Entries(InEntries) {}
};

/**
 * Flat, weak-ref-free payload broadcast on the message bus when a hub value changes (only when
 * bRepublishOnBus is enabled). Carried inside an FInstancedStruct by the bus; holds no UObject/weak
 * references so it is safe to queue for deferred dispatch.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSWORLD_API FWorldHub_ValueChangedPayload
{
	GENERATED_BODY()

	/** The scope whose value changed. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|WorldHub")
	FWorldHub_Scope Scope;

	/** The key whose value changed. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|WorldHub")
	FGameplayTag Key;

	/** The new net-friendly value (unset when cleared or when the entry is a non-net Struct kind). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|WorldHub")
	FSeam_NetValue NewValue;

	FWorldHub_ValueChangedPayload() = default;
	FWorldHub_ValueChangedPayload(const FWorldHub_Scope& InScope, const FGameplayTag& InKey, const FSeam_NetValue& InValue)
		: Scope(InScope), Key(InKey), NewValue(InValue) {}
};

/**
 * Broadcast (server and clients) whenever a single hub value changes.
 * @param Scope    The scope whose value changed.
 * @param Key      The key whose value changed.
 * @param NewValue The new net-friendly value (unset when cleared / non-net Struct kind).
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FWorldHub_OnValueChanged, FWorldHub_Scope, Scope, FGameplayTag, Key, FSeam_NetValue, NewValue);

/**
 * The central world-state query/mutation spine for a single world.
 *
 * Owns the authoritative flag registry and a map of per-scope blackboards. Implements
 * IWorldHub_Queryable (consumers query world state through a stable interface resolved from the
 * locator) and ISeam_Persistable (the save path captures/restores it generically).
 *
 * Authority model:
 *   - This subsystem is NEVER replicated. Authoritative replicated state is mirrored onto a
 *     UWorldHub_StateRepComponent (an actor-component carrier); the hub holds a non-owning weak
 *     reference to that carrier and asks it to push authoritative writes.
 *   - Every mutator guards authority AT THE TOP and early-returns on clients. Clients receive state
 *     exclusively through SyncReplicatedState (called by the carrier on replication).
 *   - Provider fallback: a query with no stored slot consults registered IWorldHub_StateProvider
 *     instances (indexed by key) so computed and stored state share one query surface.
 *
 * It self-registers under DP.Service.WorldHub (WeakObserved) so other systems resolve it by tag,
 * and optionally re-publishes value changes on the message bus with a FLAT payload (no weak refs).
 */
UCLASS()
class DESIGNPATTERNSWORLD_API UWorldHub_StateHubSubsystem
	: public UDP_WorldSubsystem
	, public IWorldHub_Queryable
	, public ISeam_Persistable
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/**
	 * UWorldSubsystem has no HasWorldAuthority(); declare our own. True on server / standalone /
	 * listen-server host (any net mode that is not a pure client). Every mutator gates on this.
	 */
	bool HasWorldAuthority() const
	{
		const UWorld* W = GetWorld();
		return W && W->GetNetMode() != NM_Client;
	}

	// ---- IWorldHub_Queryable ------------------------------------------------------------------
	virtual bool QueryValue(const FGameplayTag& Key, const FWorldHub_Scope& Scope, FInstancedStruct& Out) const override;
	virtual bool HasValue(const FGameplayTag& Key, const FWorldHub_Scope& Scope) const override;

	// ---- ISeam_Persistable --------------------------------------------------------------------
	virtual void CaptureState_Implementation(FInstancedStruct& Out) const override;
	virtual void RestoreState_Implementation(const FInstancedStruct& In) override;
	virtual FGameplayTag GetPersistenceKind_Implementation() const override;

	// ---- Blueprint-facing query convenience ---------------------------------------------------

	/** BP wrapper for HasValue at Global scope. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|WorldHub")
	bool BP_HasValueGlobal(FGameplayTag Key) const { return HasValue(Key, FWorldHub_Scope::Global()); }

	/** Read a boolean flag for a scope (with Entity/Faction -> Global fallback). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|WorldHub")
	bool QueryFlag(FGameplayTag Key, FWorldHub_Scope Scope, bool bDefault = false) const;

	/** Read a counter for a scope (with fallback). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|WorldHub")
	int64 QueryCounter(FGameplayTag Key, FWorldHub_Scope Scope, int64 Default = 0) const;

	// ---- Authoritative mutators (guard authority at TOP) --------------------------------------

	/**
	 * Set a boolean flag. AUTHORITY ONLY (no-op on clients). Mirrors onto the rep carrier if the
	 * flag replicates, fires OnValueChanged and (optionally) re-publishes on the bus.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|WorldHub")
	void SetFlag(FGameplayTag Key, bool bValue, FWorldHub_Scope Scope);

	/** Add Delta to a clamped counter; returns the new value. AUTHORITY ONLY (returns current/0 on clients). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|WorldHub")
	int64 IncrementCounter(FGameplayTag Key, int64 Delta, FWorldHub_Scope Scope);

	/** Set a net-friendly value variant under (Key, Scope). AUTHORITY ONLY. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|WorldHub")
	void SetNetValue(FGameplayTag Key, FSeam_NetValue Value, FWorldHub_Scope Scope);

	/**
	 * Set an arbitrary FInstancedStruct variable under (Key, Scope). AUTHORITY ONLY. If the value's
	 * inner type is net-friendly and the flag is defined replicable, it is mirrored to the carrier;
	 * otherwise it stays server-/save-local.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|WorldHub")
	void SetVariable(FGameplayTag Key, FInstancedStruct Value, FWorldHub_Scope Scope);

	/** Set the full value record for (Key, Scope) directly. AUTHORITY ONLY. */
	void SetValue(const FGameplayTag& Key, const FWorldHub_Scope& Scope, const FWorldHub_FlagValue& Value);

	/** Remove (Key, Scope)'s value entirely. AUTHORITY ONLY. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|WorldHub")
	void ClearValue(FGameplayTag Key, FWorldHub_Scope Scope);

	/** Read (Key, Scope)'s raw value. @return true (and fills Out) if a stored value exists. Safe on clients. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|WorldHub")
	bool GetVariable(FGameplayTag Key, FWorldHub_Scope Scope, FInstancedStruct& Out) const;

	// ---- Scoped blackboards -------------------------------------------------------------------

	/**
	 * Resolve the scoped blackboard for Scope. When bCreate is true a new one is created (and kept
	 * alive) if absent; when false a missing scope returns null. These are LOCAL, non-replicated
	 * scratch boards (created with this subsystem as outer, wired to this hub as their change sink).
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|WorldHub|Blackboard")
	UWorldHub_ScopedBlackboard* GetBlackboard(FWorldHub_Scope Scope, bool bCreate = true);

	/** Discard the blackboard for Scope (if any). @return true if one existed. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|WorldHub|Blackboard")
	bool ClearBlackboard(FWorldHub_Scope Scope);

	/**
	 * Notification sink called by a scoped blackboard when one of its values changes. Re-published on
	 * the bus (DP.Bus.WorldHub.BlackboardChanged) when bRepublishOnBus is set.
	 */
	void OnScopedBlackboardChanged(const FWorldHub_Scope& Scope, FName Key);

	// ---- State providers ----------------------------------------------------------------------

	/** Register a fallback state provider consulted by queries with no stored slot. Held weakly. */
	void RegisterStateProvider(const TScriptInterface<IWorldHub_StateProvider>& Provider);

	/** Remove a previously-registered provider. */
	void UnregisterStateProvider(const TScriptInterface<IWorldHub_StateProvider>& Provider);

	// ---- Replication carrier wiring -----------------------------------------------------------

	/**
	 * Apply a full replicated snapshot from the authoritative carrier into the local registry
	 * (CLIENT path — the "SyncFromServer" pattern). Mirrors each net entry into the registry, prunes
	 * replicated slots no longer present, fires OnValueChanged per changed key, and re-publishes on
	 * the bus if enabled. On the authority this is a no-op (authority owns the source of truth).
	 */
	void SyncReplicatedState(const FWorldHub_RepState& State);

	/** Attach the replication carrier the hub mirrors authoritative writes onto. Non-owning (weak). */
	void AttachNetCarrier(UWorldHub_StateRepComponent* Carrier);

	/** Detach the named carrier if it is the current one. Cleared automatically on Deinitialize. */
	void DetachNetCarrier(UWorldHub_StateRepComponent* Carrier);

	/** Build a full replicable snapshot of current registry state (for carrier seeding on BeginPlay). */
	void BuildRepState(FWorldHub_RepState& Out) const;

	// ---- Defaults & cross-level flush ---------------------------------------------------------

	/**
	 * Seed default entries/definitions from a flag-set data asset (AUTHORITY ONLY for the write;
	 * clients get the result via replication). Existing slots are preserved unless bOverwriteExisting.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|WorldHub")
	void LoadDefaults(UWorldHub_FlagSetDataAsset* FlagSet, bool bOverwriteExisting = false);

	/**
	 * Push this world's current save-bearing registry slots into the persistent game-instance hub so
	 * they survive level travel. AUTHORITY ONLY. Safe no-op if GameStateHub is null.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|WorldHub")
	void FlushSaveStateTo(UWorldHub_GameStateHubSubsystem* GameStateHub);

	// ---- Events & config ----------------------------------------------------------------------

	/** Fired (server and clients) whenever a single value changes. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|WorldHub")
	FWorldHub_OnValueChanged OnValueChanged;

	/**
	 * When true, every value change is also re-published on the message bus under the
	 * WorldHubNativeTags channels with a FLAT payload (no weak refs, safe to queue). Off by default
	 * to avoid bus traffic for projects that only use the direct OnValueChanged delegate.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|WorldHub|Config")
	bool bRepublishOnBus = false;

	/** Direct access to the owned flag registry (server source of truth / client mirror). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|WorldHub")
	UWorldHub_FlagRegistry* GetRegistry() const { return Registry; }

	//~ Begin UDP_WorldSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_WorldSubsystem

private:
	/** The authoritative flag registry (server) / replicated mirror (client). Instanced subobject. */
	UPROPERTY()
	TObjectPtr<UWorldHub_FlagRegistry> Registry;

	/** Per-scope blackboards, created on demand and kept alive here. */
	UPROPERTY()
	TMap<FWorldHub_Scope, TObjectPtr<UWorldHub_ScopedBlackboard>> Blackboards;

	/** Non-owning reference to the replication carrier the hub mirrors authoritative writes onto. */
	TWeakObjectPtr<UWorldHub_StateRepComponent> NetCarrier;

	/** Registered fallback providers (non-owning). Pruned of stale entries on query. */
	TArray<TWeakInterfacePtr<IWorldHub_StateProvider>> Providers;

	/** Key -> set of provider indices (into Providers) that can answer it. Rebuilt on (un)register. */
	TMap<FGameplayTag, TArray<int32>> ProviderIndexByKey;

	/** Apply an authoritative slot write: store, project & mirror to carrier, notify. AUTHORITY path. */
	void ApplyAuthoritativeWrite(const FGameplayTag& Key, const FWorldHub_Scope& Scope, const FWorldHub_FlagValue& Value);

	/** Mirror a slot's net projection onto the attached carrier (server only). */
	void PushSlotToCarrier(const FWorldHub_Scope& Scope, const FGameplayTag& Key, const FWorldHub_FlagValue& Value);

	/** Remove a slot from the attached carrier (server only). */
	void RemoveSlotFromCarrier(const FWorldHub_Scope& Scope, const FGameplayTag& Key);

	/** Fire OnValueChanged and, if enabled, re-publish on the bus with a flat payload. */
	void NotifyValueChanged(const FWorldHub_Scope& Scope, const FGameplayTag& Key, const FSeam_NetValue& NewValue);

	/** Resolve a fallback value from registered providers for (Key, Scope). */
	bool QueryProviders(const FGameplayTag& Key, const FWorldHub_Scope& Scope, FInstancedStruct& Out) const;

	/** Rebuild ProviderIndexByKey from the current Providers array, dropping stale entries. */
	void RebuildProviderIndex();

	/** Self-register under DP.Service.WorldHub (WeakObserved). */
	void RegisterSelfAsService();

	/** Project a flag value to a net value using its definition (or inference). @return true on success. */
	bool ProjectSlotToNet(const FGameplayTag& Key, const FWorldHub_FlagValue& Value, FSeam_NetValue& Out) const;
};
