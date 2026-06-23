// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "UObject/WeakInterfacePtr.h"
#include "Seam/Mod_ContentSource.h"
#include "Seam/Mod_PackValidator.h"
#include "Mod/Seam_ModResolution.h"
#include "Mod_ContentManagerSubsystem.generated.h"

class UMod_DeveloperSettings;
class UDP_MessageBusSubsystem;
class UDP_ServiceLocatorSubsystem;

/** Lifecycle state of a pack from the manager's point of view. */
UENUM(BlueprintType)
enum class EMod_PackState : uint8
{
	/** Seen by discovery, eligible, but not yet mounted. */
	Discovered,

	/** Currently mounted (plugin enabled / pak layered onto the platform file). */
	Mounted,

	/** Discovered but refused (allowlist / sandbox / validation Fail / unresolved dependency / cycle). */
	Rejected
};

/**
 * Runtime record for one pack the manager knows about. Keyed by PackId. Carries the discovery info,
 * the current state, the validation report from the last mount attempt, the resolved mount order
 * index, and the engine mount handle needed to unmount cleanly.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSMODCONTENT_API FMod_MountedPack
{
	GENERATED_BODY()

	/** The discovery-time descriptor for this pack (id, kind, disk path, mount point, source). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Mod")
	FMod_PackInfo Info;

	/** Current lifecycle state. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Mod")
	EMod_PackState State = EMod_PackState::Discovered;

	/** Validation report from the last mount attempt (empty Pass if never attempted). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Mod")
	FMod_ValidationReport LastValidation;

	/** Resolved topological mount order index (lower mounts first). -1 until order is computed. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Mod")
	int32 OrderIndex = INDEX_NONE;

	/**
	 * For Pak packs: the mount point string passed to IPlatformFilePak so the same point can be
	 * unmounted. Empty for plugin packs (unmounted by plugin name via IPluginManager).
	 */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Mod")
	FString ActiveMountPoint;
};

/**
 * Message-bus payload broadcast on mount / unmount / reject. PII-free and net-safe by construction
 * (only a tag + a couple of plain fields); local-only — never replicated. Sent as an FInstancedStruct
 * over DP.Bus.Mod.* channels.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSMODCONTENT_API FMod_PackMountEvent
{
	GENERATED_BODY()

	/** The pack this event concerns. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Mod")
	FGameplayTag PackId;

	/** New state after the operation (Mounted / Discovered after unmount / Rejected). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Mod")
	EMod_PackState State = EMod_PackState::Discovered;

	/** Validation result that produced this event (Pass/Warn for a mount, Fail for a reject). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Mod")
	EMod_ValidationResult ValidationResult = EMod_ValidationResult::Pass;
};

/**
 * GameInstance subsystem that runs the LOCAL content-pack pipeline.
 *
 * Responsibilities:
 *   - DISCOVER packs by merging every registered IMod_ContentSource (DP.Service.Mod.Source) with an
 *     IPluginManager scan and the configured discovery directories.
 *   - RESOLVE load order via a topological sort over declared dependencies, rejecting cycles and
 *     hard-missing dependencies, with a configurable tie-breaker among independent packs.
 *   - VALIDATE-BEFORE-ACTIVATE: run the validator seam (DP.Service.Mod.Validator) at every mount and
 *     reject (Fail) or warn (Warn) per its report, in addition to the manager's own hard guards
 *     (allowlist, sandbox-root, version compatibility).
 *   - MOUNT / UNMOUNT at runtime: plugin packs via FPluginManager (MountExplicitlyLoadedPlugin),
 *     pak packs via IPlatformFilePak::Mount, tracked so they unmount cleanly.
 *   - Expose mounted-pack METADATA and fire mounted / unmounted / rejected message-bus events.
 *
 * SAFETY: mod code is NEVER auto-executed — only content (assets/paks) is mounted; the allowlist and
 * sandbox roots are enforced before any mount; validation gates activation.
 *
 * GC / lifetime: this is a GameInstance subsystem that holds references to seam providers (sources,
 * validator) which may be world-scoped. Those are held WEAKLY (TWeakInterfacePtr + a weak UObject)
 * and pruned on use, never as TScriptInterface hard refs — a hard interface ref in a GI subsystem
 * outlives worlds and crashes. Discovered/mounted records are plain USTRUCTs in a UPROPERTY map.
 *
 * Pak mounts are ENVIRONMENT state, NOT save state: this subsystem deliberately does NOT implement
 * ISeam_Persistable. Mount state is rebuilt from discovery on each boot.
 */
UCLASS()
class DESIGNPATTERNSMODCONTENT_API UMod_ContentManagerSubsystem : public UDP_GameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	// ---- Source / validator registration ----

	/**
	 * Register a content source so its packs are included in discovery. Held weakly (pruned on use).
	 * Sources are also auto-discovered from the service locator (DP.Service.Mod.Source); this is the
	 * explicit native path for sources that prefer direct registration.
	 */
	void RegisterContentSource(const TScriptInterface<IMod_ContentSource>& Source);

	/** Remove a previously registered content source. Safe to call with an unknown/expired source. */
	void UnregisterContentSource(const TScriptInterface<IMod_ContentSource>& Source);

	/**
	 * ADDITIVE: install an optional resolution-policy seam this manager will CONSULT for load-order
	 * tie-breaking (it does not replace the topological dependency sort, which is always honoured). The
	 * policy is held WEAKLY (TWeakInterfacePtr, pruned on use) so a world-scoped policy cannot leak across
	 * worlds — exactly like the content-source / validator seams. Passing an empty/invalid interface (or
	 * never calling this) keeps the shipped ResolveMountOrder behaviour unchanged (the inert default).
	 *
	 * The policy is also auto-resolvable from the service locator under DP.Service.Mod.Resolution; this
	 * is the explicit native path for projects that prefer direct registration.
	 */
	void SetResolutionPolicy(const TScriptInterface<ISeam_ModResolutionPolicy>& InPolicy);

	/** Clear a previously-installed resolution policy (revert to the inert default). */
	void ClearResolutionPolicy();

	// ---- Discovery ----

	/**
	 * Re-run discovery: merge all sources + IPluginManager + discovery directories into the discovered
	 * set, filter by the allowlist policy, then resolve mount order. Does NOT mount anything. Returns
	 * the number of eligible packs discovered.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Mod")
	int32 DiscoverPacks();

	// ---- Mount / unmount ----

	/**
	 * Validate then mount a single discovered pack (and, transitively, its not-yet-mounted hard
	 * dependencies in order). Rejects on validation Fail, missing dependency, version mismatch, or
	 * sandbox violation. Returns true if the pack ends up Mounted.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Mod")
	bool MountPack(FGameplayTag PackId);

	/**
	 * Mount every discovered, eligible, validation-passing pack in resolved dependency order. Honours
	 * the mount-order tie-breaker. Returns the number of packs successfully mounted.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Mod")
	int32 MountAllDiscovered();

	/**
	 * Unmount a mounted pack. Refused (returns false, logged) if another currently-mounted pack hard-
	 * depends on it, to avoid dangling content references. Plugin packs are unmounted via
	 * IPluginManager; pak packs via IPlatformFilePak. Fires the unmounted bus event on success.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Mod")
	bool UnmountPack(FGameplayTag PackId);

	/** Unmount every mounted pack in reverse dependency order. Returns the number unmounted. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Mod")
	int32 UnmountAll();

	// ---- Queries ----

	/** True if the pack is currently mounted. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Mod")
	bool IsPackMounted(FGameplayTag PackId) const;

	/** Copy out the record for a pack (any state). Returns false if the pack is unknown. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Mod")
	bool GetPackRecord(FGameplayTag PackId, FMod_MountedPack& OutRecord) const;

	/** All currently-mounted packs, in ascending mount order. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Mod")
	TArray<FMod_MountedPack> GetMountedPacks() const;

	/** All discovered packs regardless of state (Discovered / Mounted / Rejected). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Mod")
	TArray<FMod_MountedPack> GetAllPacks() const;

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

private:
	// ---- Internal record store ----

	/** All known packs keyed by id. Plain USTRUCT values held alive by this UPROPERTY map. */
	UPROPERTY(Transient)
	TMap<FGameplayTag, FMod_MountedPack> Packs;

	/**
	 * Explicitly-registered content sources, held WEAKLY so a world-scoped source cannot be kept alive
	 * by this GI subsystem across worlds. Pruned on use. (Service-locator sources are resolved fresh
	 * each discovery and never stored.)
	 */
	TArray<TWeakInterfacePtr<IMod_ContentSource>> WeakSources;

	/**
	 * Optional resolution-policy seam, held WEAKLY so a world-scoped policy cannot be pinned alive by this
	 * GI subsystem across worlds. Pruned on use. When unset (or expired) the manager keeps its shipped
	 * ordering. Re-resolved from the locator (DP.Service.Mod.Resolution) when the explicit ref is absent.
	 */
	TWeakInterfacePtr<ISeam_ModResolutionPolicy> WeakResolutionPolicy;

	/** Resolve the resolution policy (explicit weak ref first, then the locator). Null when none. */
	TScriptInterface<ISeam_ModResolutionPolicy> ResolveResolutionPolicy() const;

	// ---- Cached engine state ----

	/** True when running on a platform/build where runtime pak mounting is meaningful (real PakPlatformFile). */
	bool bPakMountingAvailable = false;

	// ---- Discovery helpers ----

	/** Gather raw FMod_PackInfo from every source (weak + service-locator) into OutRaw. */
	void GatherFromSources(TArray<FMod_PackInfo>& OutRaw) const;

	/** Gather packs from IPluginManager (explicitly-loadable content plugins under the project). */
	void GatherFromPluginManager(TArray<FMod_PackInfo>& OutRaw) const;

	/** Scan the configured discovery directories for .uplugin / .pak packs. */
	void GatherFromDirectories(TArray<FMod_PackInfo>& OutRaw) const;

	/** Prune expired weak sources from WeakSources. */
	void PruneSources();

	// ---- Order resolution ----

	/**
	 * Topologically sort the eligible discovered packs by their declared dependencies. Detects and
	 * rejects cycles (every pack in a cycle is marked Rejected). Applies the configured tie-breaker
	 * among independent packs. Writes OrderIndex into each record.
	 */
	void ResolveMountOrder();

	// ---- Mount internals ----

	/**
	 * Core mount of one record (already eligible). Runs validation, checks dependencies are mounted,
	 * performs the engine mount, updates state, and broadcasts. Returns true on Mounted.
	 * bAllowDependencyMount lets MountPack recurse to satisfy hard deps.
	 */
	bool MountRecordInternal(FMod_MountedPack& Record, bool bAllowDependencyMount, TSet<FGameplayTag>& InProgress);

	/** Perform the actual engine mount (plugin or pak) for an already-validated record. */
	bool DoEngineMount(FMod_MountedPack& Record);

	/** Perform the actual engine unmount (plugin or pak). */
	bool DoEngineUnmount(FMod_MountedPack& Record);

	// ---- Guards ----

	/** True if the pack's disk path / mount point resolves under a configured sandbox root. */
	bool PassesSandboxPolicy(const FMod_PackInfo& Info) const;

	/** True if the host engine/game versions satisfy the pack's min-version gates. */
	bool PassesVersionGates(const FMod_PackInfo& Info, FMod_ValidationReport& InOutReport) const;

	/** Resolve the validator seam (weak, pruned on use). Null when none is registered (inert => Pass). */
	TScriptInterface<IMod_PackValidator> ResolveValidator() const;

	// ---- Bus ----

	/** Broadcast a mount/unmount/reject event on the given channel. No-op if the bus is unavailable. */
	void BroadcastMountEvent(FGameplayTag Channel, const FMod_MountedPack& Record, EMod_ValidationResult ValidationResult) const;

	/** Resolve the message bus (may be null in early/teardown contexts). */
	UDP_MessageBusSubsystem* GetBus() const;

	/** Resolve the service locator (may be null in early/teardown contexts). */
	UDP_ServiceLocatorSubsystem* GetLocator() const;

	/** Effective settings CDO; never returns null (falls back to GetDefault). */
	const UMod_DeveloperSettings* GetSettings() const;
};
