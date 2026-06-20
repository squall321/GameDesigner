// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "NativeGameplayTags.h"

/**
 * Level-director module for the DesignPatterns plugin.
 *
 * Wraps the engine's level-streaming / World Partition machinery and the core spawn Factory/Pool
 * behind clean, GameplayTag-driven APIs. This module is responsible for:
 *  - A world-scoped streaming director (ULvl_StreamingDirectorSubsystem) that loads/unloads
 *    streaming levels (and World Partition cells, where available) around registered interest
 *    sources (players/cameras) using a data-driven distance-band policy with per-frame budgets.
 *  - Spawn regions/points (ALvl_SpawnRegionVolume / ULvl_SpawnPointComponent) that publish
 *    team/tag-filtered spawn transforms through the ILvl_SpawnRegionProvider seam, feeding the
 *    AI spawn director without a hard dependency on it.
 *  - The ILvl_InterestSource / ILvl_SpawnRegionProvider seams the rest of the module composes on.
 *
 * EVERYTHING here is per-machine (local): streaming is a per-client decision. The only
 * authority-bound bit lives in the (sibling) procedural-placement area of this module; this area
 * never replicates and never persists streaming state.
 *
 * Cross-module coupling is ONLY through the Seams module (ISeam_ActivationGate, ISeam_TileProviderRead,
 * ISeam_AnalyticsSink) resolved from the service locator, and the message bus. No genre/high-level
 * module is ever hard-included. Each seam degrades to a documented inert default when unresolved.
 */
class FDesignPatternsLevelDirectorModule : public IModuleInterface
{
public:
	//~ Begin IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface
};

/**
 * Native (C++-defined) anchor tags for the level-director module.
 *
 * Service-locator keys and message-bus channels are anchored UNDER the core's DP.Service / DP.Bus
 * roots so tag-hierarchy matching works at startup. Concrete spawn-filter tags (team/faction/role)
 * are authored by the game project, NOT here. Full tag strings live in the module .cpp.
 */
namespace LvlTags
{
	// --- Service-locator keys (children of the core DP.Service root) ---
	//
	// NOTE: the activation-gate service key is OWNED by the placement area and declared in
	// DesignPatternsLevelDirectorNativeTags.h as LvlNativeTags::Service_Lvl_ActivationGate. The
	// streaming director resolves the gate under THAT shared key so the whole module shares one gate
	// registration; it is intentionally NOT redeclared here to avoid a second, divergent gate slot.

	/** Service key under which the streaming director subsystem registers itself. */
	DESIGNPATTERNSLEVELDIRECTOR_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_StreamingDirector);

	/** Service key under which spawn-region providers register (ILvl_SpawnRegionProvider). */
	DESIGNPATTERNSLEVELDIRECTOR_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_SpawnRegionProvider);

	/**
	 * Service key the streaming director uses to resolve an ISeam_AnalyticsSink. When unresolved the
	 * director's analytics are a no-op (rule 6/10 inert default).
	 */
	DESIGNPATTERNSLEVELDIRECTOR_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_AnalyticsSink);

	// --- Message-bus channels (children of the core DP.Bus root) ---

	/** Bus channel: broadcast when a streaming level/cell begins loading. */
	DESIGNPATTERNSLEVELDIRECTOR_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_StreamingLevelLoading);

	/** Bus channel: broadcast when a streaming level/cell has finished loading and is visible. */
	DESIGNPATTERNSLEVELDIRECTOR_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_StreamingLevelLoaded);

	/** Bus channel: broadcast when a streaming level/cell begins unloading. */
	DESIGNPATTERNSLEVELDIRECTOR_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_StreamingLevelUnloading);

	// --- Activation-gate KEY queried via ISeam_ActivationGate (default OPEN when gate unresolved) ---

	/** Gate that, when closed, suspends the streaming director entirely (e.g. during a cinematic). */
	DESIGNPATTERNSLEVELDIRECTOR_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Gate_StreamingEnabled);

	// --- Analytics event key (RecordAggregateEvent on the ISeam_AnalyticsSink; no-op when unresolved) ---

	/** Aggregate analytics event summarizing streaming churn over an interval. */
	DESIGNPATTERNSLEVELDIRECTOR_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Analytics_StreamingChurn);
}
