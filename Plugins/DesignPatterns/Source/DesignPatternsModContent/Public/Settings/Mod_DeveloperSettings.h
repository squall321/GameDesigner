// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "Descriptor/Mod_ContentPackDescriptor.h"
#include "Mod_DeveloperSettings.generated.h"

/** How the manager decides WHICH discovered packs are even eligible to be considered for mounting. */
UENUM(BlueprintType)
enum class EMod_AllowlistPolicy : uint8
{
	/**
	 * Only packs whose id is explicitly listed in AllowedPackIds (by exact tag or a covering ancestor)
	 * may be mounted. The safe default for a shipped game that only blesses first-party DLC.
	 */
	AllowlistOnly,

	/**
	 * Packs in DeniedPackIds are refused; everything else is eligible. Use for open modding where the
	 * project only needs to ban known-bad packs.
	 */
	DenylistOnly,

	/**
	 * Everything discovered is eligible (no id filtering). Sandbox-root and validation guards still
	 * apply. Intended for development only — never ship with this.
	 */
	AllowAll
};

/** When (if ever) the manager mounts eligible packs automatically without an explicit MountPack call. */
UENUM(BlueprintType)
enum class EMod_AutoMountPolicy : uint8
{
	/**
	 * DEFAULT. The manager discovers packs but mounts nothing on its own; the host game must call
	 * MountPack / MountAllDiscovered explicitly. Safest: no content activates without an explicit ask.
	 */
	Off,

	/**
	 * On GameInstance start the manager mounts every discovered, allowlisted, validation-passing pack
	 * in dependency order. Convenient for a curated first-party DLC set; still validate-before-activate.
	 */
	OnStartup
};

/** Tie-breaker ordering applied AFTER the hard topological (dependency) order is satisfied. */
UENUM(BlueprintType)
enum class EMod_MountOrderPolicy : uint8
{
	/** Among packs with no dependency relation, preserve discovery order (source enumeration order). */
	DiscoveryOrder,

	/** Among independent packs, sort by pack id tag name (stable, reproducible across runs). */
	Alphabetical,

	/** Among independent packs, honour the explicit ExplicitMountOrder list first, then discovery order. */
	Explicit
};

/**
 * How data-tag override conflicts between packs are resolved when no ISeam_ModResolutionPolicy is
 * registered. The default keeps the registry's shipped precedence (highest LoadPriority / latest mount).
 */
UENUM(BlueprintType)
enum class EMod_ConflictPolicy : uint8
{
	/** DEFAULT. Keep the registry's shipped precedence: highest LoadPriority wins, ties to latest mount. */
	LoadOrderPrecedence,

	/**
	 * Refuse to mount a pack that would override a data tag already provided by an earlier pack (the
	 * first pack to claim a tag keeps it). The diagnostics resolver surfaces these as conflicts.
	 */
	FirstWins,

	/** Defer entirely to a registered ISeam_ModResolutionPolicy; if none is registered, falls back to LoadOrderPrecedence. */
	PolicySeam
};

/**
 * Project-wide configuration for the DesignPatternsModContent module. Appears under
 * Project Settings -> Plugins -> Design Patterns Mod Content. Editing here requires no code.
 *
 * These are the genre-neutral SAFETY + POLICY tunables the content manager reads from its CDO: the
 * allowlist/denylist, the auto-mount policy (DEFAULT OFF — nothing mounts until asked), the sandboxed
 * content roots packs must live under, the mount-order tie-breaker, and the host engine/game versions
 * used for compatibility gating. There are no hardcoded magic numbers in code; the manager reads the
 * CDO via Get() and applies documented defensive fallbacks if the CDO is somehow null.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Design Patterns Mod Content"))
class DESIGNPATTERNSMODCONTENT_API UMod_DeveloperSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UMod_DeveloperSettings();

	//~ Begin UDeveloperSettings
	/** Group under the "Plugins" category in Project Settings. */
	virtual FName GetCategoryName() const override { return FName("Plugins"); }
	//~ End UDeveloperSettings

	// ---- Eligibility / safety ----

	/** How discovered packs are filtered for eligibility. Defaults to the strict AllowlistOnly. */
	UPROPERTY(EditAnywhere, Config, Category = "Safety")
	EMod_AllowlistPolicy AllowlistPolicy = EMod_AllowlistPolicy::AllowlistOnly;

	/**
	 * Pack ids (children of DP.Mod.Pack) that ARE allowed under AllowlistOnly. A listed ancestor tag
	 * covers all its children (e.g. DP.Mod.Pack.FirstParty allows every first-party pack). Ignored
	 * under DenylistOnly / AllowAll.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Safety", meta = (Categories = "DP.Mod.Pack"))
	FGameplayTagContainer AllowedPackIds;

	/**
	 * Pack ids (children of DP.Mod.Pack) that are REFUSED under DenylistOnly (a listed ancestor bans
	 * its whole subtree). Also honoured as a hard ban under AllowlistOnly (deny wins over allow).
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Safety", meta = (Categories = "DP.Mod.Pack"))
	FGameplayTagContainer DeniedPackIds;

	/**
	 * Sandbox roots: absolute or project-relative directories that pak content roots / mount points
	 * MUST resolve under. A pack whose disk path or mount point escapes every sandbox root is refused
	 * (anti path-traversal). Plugin packs discovered by IPluginManager are additionally constrained to
	 * the project's own plugin dirs. If empty, the manager falls back to a documented default of the
	 * project "Mods" directory (defensive — never an empty/unbounded sandbox).
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Safety", meta = (RelativeToGameDir))
	TArray<FDirectoryPath> SandboxContentRoots;

	// ---- Discovery / mounting ----

	/**
	 * When the manager auto-mounts. DEFAULT Off: discovery happens but no pack mounts until an explicit
	 * MountPack / MountAllDiscovered call. This is the "nothing activates without consent" default.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Mounting")
	EMod_AutoMountPolicy AutoMountPolicy = EMod_AutoMountPolicy::Off;

	/**
	 * Filesystem directories the built-in disk source scans for packs (.uplugin folders / .pak files).
	 * These are in addition to any IMod_ContentSource registered at runtime. Empty is valid (the
	 * manager then relies only on IPluginManager + registered sources).
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Mounting", meta = (RelativeToGameDir))
	TArray<FDirectoryPath> DiscoveryDirectories;

	/** Tie-breaker order among packs with no dependency relation (the topological order always wins). */
	UPROPERTY(EditAnywhere, Config, Category = "Mounting")
	EMod_MountOrderPolicy MountOrderPolicy = EMod_MountOrderPolicy::DiscoveryOrder;

	/**
	 * Explicit ordering used when MountOrderPolicy is Explicit. Packs listed earlier mount earlier
	 * (subject to dependencies always coming first). Packs absent from this list mount after, in
	 * discovery order.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Mounting", meta = (EditCondition = "MountOrderPolicy == EMod_MountOrderPolicy::Explicit", Categories = "DP.Mod.Pack"))
	TArray<FGameplayTag> ExplicitMountOrder;

	/**
	 * Mount-priority offset for raw .pak packs passed to IPlatformFilePak::Mount. Higher mounts later
	 * and therefore wins file-path collisions. Pak packs are offset from plugin packs so designers can
	 * reason about override layering. Defensive fallback documented at the call site if the CDO is null.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Mounting", meta = (ClampMin = "0"))
	int32 BasePakMountPriority = 1000;

	// ---- Compatibility ----

	/**
	 * The host GAME (project) version advertised to packs' MinGameVersion gates. Set per release so old
	 * packs that require a newer game are refused. Zero disables the game-version gate.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Compatibility")
	FMod_SemVer HostGameVersion;

	/**
	 * Optional override of the host ENGINE version used for MinEngineVersion gating. When zero the
	 * manager reads the real running engine version (FEngineVersion) instead — the normal case.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Compatibility")
	FMod_SemVer HostEngineVersionOverride;

	// ---- Trust / signing (additive deepening) ----

	/**
	 * When true a pack must pass the signature-verifier seam (ISeam_ModSignatureVerifier) with a verdict
	 * that AllowsMount before the manifest path lets it mount. With no verifier registered the inert
	 * default returns HashOk (integrity-only), which still allows the mount — so enabling this without a
	 * verifier degrades to hash integrity, never an unconditional block. DEFAULT off (no signing required).
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Trust")
	bool bRequireSignedPacks = false;

	/**
	 * Signer ids (children of a project DP.Mod.Signer.* namespace) the host trusts. Advisory metadata for
	 * the host's verifier seam; the genre-neutral module does no crypto itself. A listed ancestor covers
	 * its subtree.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Trust")
	FGameplayTagContainer TrustedSignerIds;

	// ---- Editor hot-reload (additive deepening) ----

	/**
	 * When true (and only in an editor/dev build) the hot-reload subsystem watches the discovery
	 * directories and re-discovers / re-mounts packs on disk change. Never auto-executes code; re-mounts
	 * still go through validate-before-activate. DEFAULT off.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "HotReload")
	bool bEnableEditorHotReload = false;

	/**
	 * Debounce window (seconds) coalescing a burst of file-change notifications into one re-discover so a
	 * multi-file save does not re-scan repeatedly. Defensive fallback applied at the call site if zero.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "HotReload", meta = (ClampMin = "0.0"))
	float HotReloadDebounceSeconds = 0.5f;

	// ---- Catalog / workshop bridge (additive deepening) ----

	/**
	 * Folder the default local-folder catalog source treats as its "store". Items here can be "downloaded"
	 * (sandbox-constrained copy) into a discovery directory. MUST resolve under a configured sandbox root
	 * just like any other content path; an empty/escaping value disables the default catalog source.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Catalog", meta = (RelativeToGameDir))
	FDirectoryPath LocalCatalogFolder;

	/**
	 * The discovery directory index (into DiscoveryDirectories) the default catalog source copies a
	 * downloaded item INTO. Clamped to a valid index at the call site; when DiscoveryDirectories is empty
	 * the catalog source is inert. This keeps every download destination inside the sandbox.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Catalog", meta = (ClampMin = "0"))
	int32 CatalogDownloadDirectoryIndex = 0;

	// ---- Conflict resolution (additive deepening) ----

	/** How data-tag override conflicts between packs are resolved. DEFAULT keeps the registry precedence. */
	UPROPERTY(EditAnywhere, Config, Category = "Mounting")
	EMod_ConflictPolicy ConflictResolutionPolicy = EMod_ConflictPolicy::LoadOrderPrecedence;

	// ---- Diagnostics ----

	/** When true the content manager logs at Verbose (discovery, ordering, each mount/unmount). */
	UPROPERTY(EditAnywhere, Config, Category = "Diagnostics")
	bool bVerboseLogging = false;

	/** Convenience accessor (never null in a running game; the CDO is populated from the project ini). */
	static const UMod_DeveloperSettings* Get();

	/**
	 * Decide whether a pack id is eligible under the current allowlist/denylist policy. Deny always
	 * wins; allowlist membership is by exact tag or covering ancestor. Used by the manager before any
	 * mount.
	 */
	bool IsPackIdEligible(const FGameplayTag& PackId) const;
};
