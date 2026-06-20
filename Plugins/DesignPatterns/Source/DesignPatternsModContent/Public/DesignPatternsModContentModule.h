// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "NativeGameplayTags.h"

/**
 * Mod / DLC / content-pack runtime module for the DesignPatterns plugin.
 *
 * Genre-agnostic, LOCAL / per-machine content-pack pipeline. It WRAPS the engine's plugin and
 * pak systems behind a clean, tag-keyed, data-driven facade:
 *   - IMod_ContentSource     : the "where do packs come from" seam (disk dir, store, embedded list).
 *   - UMod_ContentPackDescriptor : a UDP_DataAsset describing one pack (id, version, deps, roots,
 *                                  min-engine / min-game requirements).
 *   - UMod_ContentManagerSubsystem : a GameInstance subsystem that discovers packs via the sources
 *                                  + IPluginManager, mounts/unmounts them at runtime over
 *                                  IPlatformFilePak / FPluginManager, resolves load order by a
 *                                  topological dependency sort (rejecting cycles / missing deps),
 *                                  VALIDATES every pack before activation, and fires mounted /
 *                                  unmounted message-bus events.
 *
 * SAFETY is structural, not optional:
 *   - Mod code is NEVER auto-executed: the manager mounts content (assets/paks) only; it never
 *     loads or runs an arbitrary module/DLL out of a pack. Activation of any behaviour is the
 *     host game's explicit decision over already-validated, sandboxed content.
 *   - VALIDATE-BEFORE-ACTIVATE: each pack is run through the validator seam at mount; a hard
 *     failure rejects the mount, a soft warning is logged and surfaced in metadata.
 *   - Allowlist + sandboxed content roots: only packs whose id matches the configured allowlist
 *     policy and whose roots live under sandboxed mount points are ever mounted.
 *
 * Pak mounts are ENVIRONMENT state (per machine, per install), NOT gameplay save state: they are
 * never persisted through ISeam_Persistable and never "restored" on load — re-discovery on each
 * boot is the source of truth.
 *
 * The module depends only on the core "DesignPatterns" module and the shared "DesignPatternsSeams"
 * contracts (plus the engine Projects / PakFile / AssetRegistry modules it wraps). It never
 * hard-includes another Wave-2 / Wave-1 / genre / sibling module; all cross-module coupling is via
 * seams resolved from the service locator and via the message bus. The module is independently
 * removable: with no content source and the default auto-mount policy OFF it does nothing.
 */
class FDesignPatternsModContentModule : public IModuleInterface
{
public:
	//~ Begin IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface
};

/**
 * Native (C++-defined) anchor tags for the DesignPatternsModContent module.
 *
 * These are ROOT / anchor tags plus the small set of stable leaf keys this module itself needs
 * (its service-locator key and its bus channels). Concrete PACK ids are authored by the game
 * project / content packs as CHILD tags under DP.Mod.Pack; anchoring the roots here guarantees the
 * hierarchy exists at startup so tag-hierarchy matching always works.
 *
 * Tag layout:
 *   DP.Mod                    - module root (umbrella for everything below).
 *   DP.Mod.Pack.*             - per-pack identity keys. PROJECT / PACK-AUTHORED leaves.
 *   DP.Mod.Source.*           - content-source identity keys (disk / store / embedded). PROJECT-AUTHORED.
 *   DP.Service.ModContent     - service-locator key the content manager registers itself under.
 *   DP.Service.Mod.Validator  - service-locator key the pack validator (sibling area) registers under.
 *   DP.Service.Mod.Source     - service-locator key a content source registers under.
 *   DP.Bus.Mod                - message-bus root for this module's channels.
 *   DP.Bus.Mod.Mounted        - broadcast when a pack is successfully mounted (payload FMod_PackMountEvent).
 *   DP.Bus.Mod.Unmounted      - broadcast when a pack is unmounted (payload FMod_PackMountEvent).
 *   DP.Bus.Mod.Rejected       - broadcast when a pack is rejected at validate-before-activate (payload FMod_PackMountEvent).
 */
namespace ModTags
{
	/** Module umbrella root: DP.Mod. Anchors the whole mod/content tag hierarchy. */
	DESIGNPATTERNSMODCONTENT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Mod);

	/** Root for per-pack identity keys (DP.Mod.Pack.MyCoolMap ...). PACKS author the leaves. */
	DESIGNPATTERNSMODCONTENT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Pack);

	/** Root for content-source identity keys (DP.Mod.Source.LocalDisk ...). PROJECT authors the leaves. */
	DESIGNPATTERNSMODCONTENT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Source);

	/** Service-locator key the content manager registers itself under (child of DP.Service). */
	DESIGNPATTERNSMODCONTENT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_ModContent);

	/** Service-locator key the pack validator (sibling area) registers under; resolved at mount. */
	DESIGNPATTERNSMODCONTENT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_Validator);

	/** Service-locator key a content source registers under so the manager can enumerate it. */
	DESIGNPATTERNSMODCONTENT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_Source);

	/** Root for message-bus channels this module participates in (children of DP.Bus). */
	DESIGNPATTERNSMODCONTENT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus);

	/** Bus channel broadcast when a pack is successfully mounted (payload FMod_PackMountEvent). */
	DESIGNPATTERNSMODCONTENT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Mounted);

	/** Bus channel broadcast when a pack is unmounted (payload FMod_PackMountEvent). */
	DESIGNPATTERNSMODCONTENT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Unmounted);

	/** Bus channel broadcast when a pack is rejected at validate-before-activate (payload FMod_PackMountEvent). */
	DESIGNPATTERNSMODCONTENT_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Rejected);
}
