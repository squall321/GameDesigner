// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "Engine/AssetManager.h"
#include "GameplayTagContainer.h"
#include "Mod_ContentRegistrySubsystem.generated.h"

class UDP_DataRegistrySubsystem;
class UDP_DataAsset;

/**
 * One tag->asset override contributed by a single mounted content pack.
 *
 * A pack that wants to ADD a brand-new data tag, or OVERRIDE an existing core/base data asset under
 * the same DataTag, contributes one of these per asset. The override is recorded LOAD-FREE: only the
 * FPrimaryAssetId / soft path is kept here, never a hard UObject pointer, so registering an override
 * never force-loads pack content (mirrors the core UDP_DataRegistrySubsystem load-free indexing
 * policy). The asset is loaded lazily only when a caller actually resolves it.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSMODCONTENT_API FMod_AssetOverride
{
	GENERATED_BODY()

	/** The data identity this override targets (matches UDP_DataAsset::DataTag). */
	UPROPERTY(BlueprintReadOnly, Category = "ModContent|Registry")
	FGameplayTag DataTag;

	/** The overriding asset, addressed by primary asset id (no load). Invalid id = malformed entry. */
	UPROPERTY(BlueprintReadOnly, Category = "ModContent|Registry")
	FPrimaryAssetId AssetId;

	/** The overriding asset's soft path, kept so we can resolve/load without the asset manager. */
	UPROPERTY(BlueprintReadOnly, Category = "ModContent|Registry")
	FSoftObjectPath AssetPath;

	/** Identity of the pack that contributed this override (its mod id tag). Used for unmount removal. */
	UPROPERTY(BlueprintReadOnly, Category = "ModContent|Registry")
	FGameplayTag SourcePackId;

	/**
	 * Override precedence. The pack's load-order priority is copied here so that, when several mounted
	 * packs override the SAME DataTag, the highest LoadPriority wins (ties broken by latest mount). A
	 * pack mounted LATER but with EQUAL priority still wins — "later/higher-priority load order wins".
	 */
	UPROPERTY(BlueprintReadOnly, Category = "ModContent|Registry")
	int32 LoadPriority = 0;
};

/**
 * Tag->asset OVERRIDE layer stacked on top of the core UDP_DataRegistrySubsystem.
 *
 * PURPOSE
 *   A mounted content pack can ADD a new UDP_DataAsset under a fresh DataTag, or OVERRIDE an existing
 *   base asset that already carries that DataTag, without touching shipped content. This subsystem is
 *   the authoritative front door for "given a DataTag, which asset wins?": it consults the override
 *   layer first and falls through to the base registry when no pack overrides the tag.
 *
 * PRECEDENCE POLICY (documented, deterministic)
 *   For a given DataTag the winning override is the contributed FMod_AssetOverride with the highest
 *   LoadPriority; ties are broken by mount order (the most recently mounted pack wins). When NO pack
 *   overrides the tag, resolution falls through to UDP_DataRegistrySubsystem (base content wins by
 *   default — packs are additive/override, never subtractive of the base unless they re-tag).
 *
 * CORE HOOK
 *   The intended integration is to register THIS subsystem as the core data registry's override
 *   provider so that even direct callers of UDP_DataRegistrySubsystem::FindByTag transparently get the
 *   overridden asset. The shipped UDP_DataRegistrySubsystem does NOT yet expose such a hook, so:
 *     - RegisterOverrideProvider()/UnregisterOverrideProvider() are the forward-compatible seam: when a
 *       future core build adds the extension point this subsystem wires itself in there.
 *     - Until then, callers MUST resolve mod-aware content THROUGH THIS subsystem (FindOverriddenAsset /
 *       ResolveAsset / ResolveAssetId) — it is the documented front door. This is logged once on init.
 *
 * REBUILD
 *   The override map is rebuilt from the set of currently-mounted packs whenever a pack mounts or
 *   unmounts. The manager area drives mount/unmount and calls ApplyPackOverrides / RemovePackOverrides
 *   (or the bus-channel equivalent). Rebuild is load-free.
 *
 * SCOPE
 *   GameInstance-scoped (overrides survive level travel; pak mounts are per-machine env state). Holds
 *   NO hard UObject refs to pack assets — only ids/paths — so it never pins pack packages in memory.
 */
UCLASS()
class DESIGNPATTERNSMODCONTENT_API UMod_ContentRegistrySubsystem : public UDP_GameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/**
	 * Resolve the winning asset id for Tag through the override layer.
	 *
	 * If one or more mounted packs override Tag, returns the highest-precedence override's id (no load).
	 * Otherwise falls through to the base UDP_DataRegistrySubsystem::ResolveAssetId. Returns an invalid
	 * id if neither layer knows the tag.
	 */
	UFUNCTION(BlueprintCallable, Category = "ModContent|Registry")
	FPrimaryAssetId FindOverriddenAsset(FGameplayTag Tag) const;

	/** True if ANY mounted pack currently overrides Tag (does not consider the base registry). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "ModContent|Registry")
	bool HasOverride(FGameplayTag Tag) const;

	/**
	 * Front-door resolve: the winning asset id for Tag, identical to FindOverriddenAsset but named to
	 * make the "resolve through the registry" call-site intent explicit for non-override callers too.
	 */
	UFUNCTION(BlueprintCallable, Category = "ModContent|Registry")
	FPrimaryAssetId ResolveAssetId(FGameplayTag Tag) const { return FindOverriddenAsset(Tag); }

	/**
	 * Front-door resolve + synchronous load of the winning asset for Tag.
	 *
	 * Resolves through the override layer (then base fall-through) and synchronously loads the resulting
	 * asset. Returns nullptr if the tag is unknown to both layers or the package fails to load. This is
	 * the call game code should prefer over UDP_DataRegistrySubsystem::FindByTag when mod overrides must
	 * be honoured.
	 */
	UFUNCTION(BlueprintCallable, Category = "ModContent|Registry")
	UDP_DataAsset* ResolveAsset(FGameplayTag Tag) const;

	/** Templated convenience: resolve through overrides and cast to a concrete UDP_DataAsset subclass. */
	template <typename T>
	T* Resolve(const FGameplayTag& Tag) const
	{
		static_assert(TIsDerivedFrom<T, UDP_DataAsset>::IsDerived, "T must derive from UDP_DataAsset");
		return Cast<T>(ResolveAsset(Tag));
	}

	/** Every DataTag that at least one mounted pack currently overrides. Order unspecified. */
	UFUNCTION(BlueprintCallable, Category = "ModContent|Registry")
	TArray<FGameplayTag> ListOverriddenTags() const;

	/**
	 * Additive read accessor: every contributed override for Tag (the full override CHAIN for that tag),
	 * sorted descending by effective precedence (highest LoadPriority first, ties to the later-mounted
	 * pack) so element 0 is the current winner. Returns copies of the private store — purely additive,
	 * never reaches into private state from outside. Empty when no pack overrides Tag.
	 *
	 * This is the front door for the content-query / override-chain inspector area, which must never
	 * touch AllOverrides / WinningByTag directly.
	 */
	UFUNCTION(BlueprintCallable, Category = "ModContent|Registry")
	TArray<FMod_AssetOverride> GetOverridesForTag(FGameplayTag Tag) const;

	/**
	 * Additive read accessor: a flat copy of EVERY contributed override across all packs (in no
	 * particular order). For tooling that wants to walk the whole override layer (asset-origin mapping,
	 * conflict inspection). Load-free; copies only.
	 */
	UFUNCTION(BlueprintCallable, Category = "ModContent|Registry")
	TArray<FMod_AssetOverride> GetAllOverrides() const;

	/** Number of tags currently overridden by mounted packs. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "ModContent|Registry")
	int32 NumOverrides() const { return WinningByTag.Num(); }

	// ---- Mount/unmount driven mutation (called by the manager area) ----

	/**
	 * Contribute (or refresh) the full override set for one mounted pack and recompute the winners.
	 *
	 * Replaces any previously-recorded overrides from the same SourcePackId (so a remount is idempotent),
	 * stores the new contributions, and rebuilds the per-tag winner table under the precedence policy.
	 * Load-free. Broadcasts the override-changed bus channel.
	 *
	 * @param PackId       Stable mod id of the contributing pack (FMod_AssetOverride::SourcePackId).
	 * @param Overrides    The pack's tag->asset override contributions (already validation-passed).
	 */
	void ApplyPackOverrides(FGameplayTag PackId, const TArray<FMod_AssetOverride>& Overrides);

	/**
	 * Remove every override contributed by PackId (pack unmount) and recompute winners. Idempotent: a
	 * pack id with no recorded overrides is a no-op. Broadcasts the override-changed bus channel.
	 */
	void RemovePackOverrides(FGameplayTag PackId);

	/** Drop ALL contributed overrides from every pack and clear the winner table (e.g. on teardown). */
	void ClearAllOverrides();

	// ---- Core override-provider hook (forward-compatible seam) ----

	/**
	 * Register THIS subsystem as the core data registry's override provider.
	 *
	 * Attempts to wire into the documented UDP_DataRegistrySubsystem override-provider extension if the
	 * running core build exposes it (resolved reflectively so this module compiles against cores that do
	 * NOT have the hook). When the hook is absent this records that the core extension is required and
	 * leaves THIS subsystem as the explicit front door (callers use ResolveAsset/FindOverriddenAsset).
	 *
	 * Returns true if the provider was actually installed into the core registry; false if the core hook
	 * is unavailable (front-door mode). Either way the override layer remains fully functional.
	 */
	UFUNCTION(BlueprintCallable, Category = "ModContent|Registry")
	bool RegisterOverrideProvider();

	/** Reverse of RegisterOverrideProvider; safe to call when no provider was installed. */
	UFUNCTION(BlueprintCallable, Category = "ModContent|Registry")
	void UnregisterOverrideProvider();

	/** True if this subsystem successfully installed itself into a core override-provider hook. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "ModContent|Registry")
	bool IsWiredIntoCoreHook() const { return bWiredIntoCoreHook; }

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

private:
	/** Resolve (and cache) the core data registry for fall-through; null-safe, re-resolved if stale. */
	UDP_DataRegistrySubsystem* ResolveBaseRegistry() const;

	/** Recompute WinningByTag from AllOverrides under the precedence policy. Load-free. */
	void RecomputeWinners();

	/** Broadcast the "mod overrides changed" message-bus channel (best-effort; no-op if bus absent). */
	void BroadcastOverridesChanged() const;

	/**
	 * Every contributed override, grouped by source pack id. One pack contributes many overrides; the
	 * winner table is derived from the union of all these. Transient/per-machine.
	 */
	TMap<FGameplayTag, TArray<FMod_AssetOverride>> AllOverrides;

	/**
	 * The resolved winner per DataTag after applying the precedence policy. This is what
	 * FindOverriddenAsset reads. Rebuilt by RecomputeWinners on every mount/unmount.
	 */
	TMap<FGameplayTag, FMod_AssetOverride> WinningByTag;

	/**
	 * Monotonic mount sequence so equal-priority ties resolve to the LATER-mounted pack. Incremented
	 * each ApplyPackOverrides; the sequence captured at apply time is stored alongside winners.
	 */
	uint64 MountSequenceCounter = 0;

	/** Per-pack mount sequence captured at ApplyPackOverrides time, used as the priority tie-breaker. */
	TMap<FGameplayTag, uint64> PackMountSequence;

	/** Cached base registry; re-resolved when stale (GameInstance lifetime, so normally stable). */
	UPROPERTY(Transient)
	mutable TObjectPtr<UDP_DataRegistrySubsystem> CachedBaseRegistry = nullptr;

	/** True once this subsystem is installed into a core override-provider hook (vs front-door mode). */
	bool bWiredIntoCoreHook = false;
};
