// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Mod_ContentSource.generated.h"

class UMod_ContentPackDescriptor;

/**
 * Where a discovered pack physically lives, so the manager knows HOW to mount it. A pack is mounted
 * either as a UE plugin (preferred — gives the engine a real content root + mount point) or as a raw
 * .pak file layered onto the platform file system.
 */
UENUM(BlueprintType)
enum class EMod_PackKind : uint8
{
	/** Unknown / not yet probed. Treated as inert (never auto-mounted). */
	Unknown,

	/**
	 * A UE plugin (.uplugin) discovered via IPluginManager. Mounted by enabling/mounting the plugin,
	 * which registers its /PackName/ content root with the engine. This is the safe, first-class path.
	 */
	Plugin,

	/**
	 * A standalone .pak (+ optional .ucas/.utoc) file on disk. Mounted via IPlatformFilePak onto a
	 * sandboxed mount point. Carries content only; the manager never treats a pak as executable.
	 */
	Pak
};

/**
 * One pack as reported by a content SOURCE before the manager has resolved order or mounted it.
 *
 * This is the discovery-time descriptor: a stable tag identity, where the pack lives on disk, what
 * kind it is, and (optionally) a resolved authoring descriptor asset. It is plain data — no engine
 * handles, no executable references — so a source can build it cheaply by scanning a directory.
 *
 * Identity is a FGameplayTag under DP.Mod.Pack (the same id authored in the pack's
 * UMod_ContentPackDescriptor). The manager keys everything (load order, dependency graph, mounted
 * metadata, bus events) off PackId.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSMODCONTENT_API FMod_PackInfo
{
	GENERATED_BODY()

	/** Stable pack identity (child of DP.Mod.Pack). Must be valid; duplicates across sources are rejected. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Mod")
	FGameplayTag PackId;

	/** How the pack is physically packaged (plugin vs raw pak). Drives the mount path the manager takes. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Mod")
	EMod_PackKind Kind = EMod_PackKind::Unknown;

	/**
	 * For Plugin packs: the engine plugin name (the .uplugin base name) to mount via IPluginManager.
	 * Empty for Pak packs.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Mod")
	FString PluginName;

	/**
	 * Absolute path on disk. For Plugin packs this is the .uplugin file; for Pak packs this is the
	 * .pak file. Used both to mount and to enforce the sandbox-root policy.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Mod")
	FString DiskPath;

	/**
	 * The engine mount point a Pak pack's contents are exposed under (e.g. "../../../<Project>/Mods/Foo/").
	 * Empty for Plugin packs (the engine derives a /PluginName/ root itself). Must resolve under a
	 * configured sandbox root or the manager refuses the mount.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Mod")
	FString MountPoint;

	/** Source identity (child of DP.Mod.Source) that reported this pack — for diagnostics / provenance. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Mod")
	FGameplayTag SourceId;

	/**
	 * Resolved authoring descriptor, if the source could load one (it carries deps / min-versions used
	 * by the dependency sort and validator). Weak so discovery does not pin descriptors in memory; the
	 * manager re-resolves / loads as needed. May be null for a bare pak with no embedded descriptor.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Mod")
	TWeakObjectPtr<UMod_ContentPackDescriptor> Descriptor;

	/** True if PackId is valid and the disk path is non-empty — the minimum for the manager to consider it. */
	bool IsUsable() const { return PackId.IsValid() && !DiskPath.IsEmpty() && Kind != EMod_PackKind::Unknown; }
};

UINTERFACE(BlueprintType, MinimalAPI)
class UMod_ContentSource : public UInterface
{
	GENERATED_BODY()
};

/**
 * Seam: a provider of discoverable content packs. WHERE packs come from is decoupled from the
 * manager that mounts them — a source might scan a local "Mods" directory, query a platform store /
 * UGC backend, or return a fixed embedded list. The manager enumerates every source registered under
 * DP.Service.Mod.Source (plus its own IPluginManager scan) and merges the results.
 *
 * Sources do plain DISCOVERY only: they never mount, never execute, and never touch engine mount
 * state. They build FMod_PackInfo records cheaply (a directory scan, a manifest read). The manager
 * owns all mount/validate/order decisions.
 *
 * Inert default: if no source is registered, the manager still works off its IPluginManager scan and
 * configured content roots; the module simply discovers fewer packs. A source must be resolvable via
 * TScriptInterface and is held weakly by the manager (pruned on use) so it cannot leak across worlds.
 */
class DESIGNPATTERNSMODCONTENT_API IMod_ContentSource
{
	GENERATED_BODY()

public:
	/**
	 * Append every pack this source currently knows about to OutPacks. Implementations must be
	 * game-thread, side-effect-free (no mounting), and tolerant of being called repeatedly (a
	 * re-scan). Return the number of packs appended.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Mod")
	int32 EnumeratePacks(UPARAM(ref) TArray<FMod_PackInfo>& OutPacks);

	/** Stable identity of this source (child of DP.Mod.Source), used for provenance and de-duplication. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Mod")
	FGameplayTag GetSourceId() const;
};
