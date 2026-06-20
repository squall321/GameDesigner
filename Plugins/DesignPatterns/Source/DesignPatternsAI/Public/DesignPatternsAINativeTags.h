// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

/**
 * Native (C++-defined) anchor tags for the DesignPatternsAI module.
 *
 * Only ROOT/anchor tags are declared here so the hierarchy is guaranteed to exist at startup
 * (tag-hierarchy matching on the message bus depends on the parents existing). Concrete leaf
 * channels, wave ids, role ids, region ids and difficulty ids are authored by the GAME project
 * as child tags in its tag config — this module never bakes gameplay-specific leaf tags.
 *
 * Channel layout (all under the core DP.Bus root, so a listener on `DP.Bus` still receives them):
 *   DP.Bus.AI.Wave.Started   — a wave began spawning (payload FAI_WaveEventPayload).
 *   DP.Bus.AI.Wave.Completed — every entry in a wave has been spawned (payload FAI_WaveEventPayload).
 *   DP.Bus.AI.Wave.Cleared   — every budgeted participant from a wave is dead (payload FAI_WaveEventPayload).
 *   DP.Bus.AI.Encounter.Activated / .Completed — encounter lifecycle (payload FAI_EncounterEventPayload).
 *   DP.Bus.AI.Squad.Formed / .Dissolved        — squad lifecycle (payload FAI_SquadEventPayload).
 *
 * Service-locator keys (anchored under the core DP.Service root):
 *   DP.Service.AI.Squad         — the UAI_SquadSubsystem registers itself (WeakObserved) here so other
 *                                 systems resolve IAI_Squad without depending on the concrete subsystem.
 *   DP.Service.AI.SpawnDirector — the UAI_SpawnDirectorSubsystem registers itself (WeakObserved).
 *   DP.Service.AI.SpawnRegions  — OPTIONAL: a level/game-authored ILvl_SpawnRegionProvider may register
 *                                 here; the spawn director resolves it and falls back to its own point
 *                                 list when absent.
 */
namespace AINativeTags
{
	// ---- Message-bus channel anchors (children of the core DP.Bus root) ----

	/** Root of every AI bus channel: DP.Bus.AI.* */
	DESIGNPATTERNSAI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_AI);

	/** A wave began spawning. */
	DESIGNPATTERNSAI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_AI_Wave_Started);

	/** Every entry in a wave has finished spawning. */
	DESIGNPATTERNSAI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_AI_Wave_Completed);

	/** Every budgeted participant spawned by a wave is dead/returned. */
	DESIGNPATTERNSAI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_AI_Wave_Cleared);

	/** An encounter passed its gate conditions and began running its waves. */
	DESIGNPATTERNSAI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_AI_Encounter_Activated);

	/** An encounter ran all of its waves to completion (or was cleared). */
	DESIGNPATTERNSAI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_AI_Encounter_Completed);

	/** A squad was formed by the squad subsystem. */
	DESIGNPATTERNSAI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_AI_Squad_Formed);

	/** A squad was dissolved. */
	DESIGNPATTERNSAI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_AI_Squad_Dissolved);

	// ---- Service-locator key anchors (children of the core DP.Service root) ----

	/** Key under which the squad subsystem self-registers (resolves IAI_Squad). */
	DESIGNPATTERNSAI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_AI_Squad);

	/** Key under which the spawn director self-registers. */
	DESIGNPATTERNSAI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_AI_SpawnDirector);

	/** Optional key a game-authored ILvl_SpawnRegionProvider registers under. */
	DESIGNPATTERNSAI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_AI_SpawnRegions);

	// ---- Perception / Behavior / Threat area (this area's leaf channels + vocabulary roots) ----

	/** Broadcast (locally) when an agent's brain selects a new decision tag (payload FAI_DecisionChangedPayload). */
	DESIGNPATTERNSAI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_AI_DecisionChanged);

	/** Broadcast (locally) when a perception component gains/updates/loses a percept (payload FAI_PerceptUpdatedPayload). */
	DESIGNPATTERNSAI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_AI_PerceptUpdated);

	/** Broadcast (locally) when an agent's top-threat entity changes (payload FAI_ThreatChangedPayload). */
	DESIGNPATTERNSAI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_AI_ThreatChanged);

	/** Root of the percept vocabulary; children classify the producing sense. */
	DESIGNPATTERNSAI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(AI_Percept);

	/** A sight stimulus (engine AISense_Sight). */
	DESIGNPATTERNSAI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(AI_Percept_Sight);

	/** A hearing stimulus (engine AISense_Hearing). */
	DESIGNPATTERNSAI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(AI_Percept_Hearing);

	/** A damage stimulus (engine AISense_Damage). */
	DESIGNPATTERNSAI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(AI_Percept_Damage);

	/** Root of the squad/tactical role vocabulary used by the behavior bridge and threat table. */
	DESIGNPATTERNSAI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(AI_Role);
}
