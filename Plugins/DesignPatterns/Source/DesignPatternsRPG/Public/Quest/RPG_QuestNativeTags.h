// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

/**
 * Native (C++-defined) anchor tags for the RPG branching-quest + journal layer.
 *
 * Only ROOTS / channel anchors are defined here; concrete quest/stage/objective keys (RPG.Quest.*,
 * RPG.Quest.Stage.*, RPG.Objective.*) are authored by the project and its data assets.
 *
 *  - DP.Bus.RPG.Quest.*   observer-only message-bus channels the quest layer broadcasts on (UI/audio).
 *  - DP.Bus.RPG.Objective.* objective-kill/collect/etc. channels objectives may LISTEN to. The concrete
 *                          gameplay channels (a kill feed, a pickup event) are project-authored under the
 *                          DP.Bus root; these are convenience anchors so listeners hierarchy-match.
 *  - DP.WorldHub.RPG.Stage.*  / DP.WorldHub.RPG.Lore.*  hub-key roots under which stage-visit flags and
 *                          lore-unlock flags are stored (so they replicate + save through the hub).
 *  - DP.Service.RPG.QuestGraph  service-locator key for the objective tracker (optional discovery).
 *  - DP.Persist.RPG.QuestGraph  persistence-kind tag for the quest-graph save record.
 */
namespace RPG_QuestNativeTags
{
	// --- Quest lifecycle bus channels (children of the core DP.Bus root) ---

	/** Bus channel: a quest graph was accepted/activated. Payload: FRPG_QuestBusEvent. */
	DESIGNPATTERNSRPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_RPG_Quest_Activated);

	/** Bus channel: a quest advanced to a new stage. Payload: FRPG_QuestBusEvent (StageTag in NodeTag). */
	DESIGNPATTERNSRPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_RPG_Quest_StageAdvanced);

	/** Bus channel: an objective made progress. Payload: FRPG_QuestBusEvent (objective in NodeTag). */
	DESIGNPATTERNSRPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_RPG_Quest_ObjectiveProgress);

	/** Bus channel: a quest completed. Payload: FRPG_QuestBusEvent. */
	DESIGNPATTERNSRPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_RPG_Quest_Completed);

	/** Bus channel: a quest failed (time limit / explicit fail outcome). Payload: FRPG_QuestBusEvent. */
	DESIGNPATTERNSRPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_RPG_Quest_Failed);

	/** Bus channel: a piece of lore was unlocked. Payload: FRPG_QuestBusEvent (lore tag in NodeTag). */
	DESIGNPATTERNSRPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_RPG_Journal_LoreUnlocked);

	// --- Objective input channel roots (objectives LISTEN here; project authors concrete leaves) ---

	/** Root anchor for gameplay "kill" notifications a KillTag objective listens to. */
	DESIGNPATTERNSRPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_RPG_Objective_KillRoot);

	/** Root anchor for "reached location" notifications a ReachLocation objective listens to. */
	DESIGNPATTERNSRPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_RPG_Objective_ReachRoot);

	/** Root anchor for "escort target" lifecycle notifications an Escort objective listens to. */
	DESIGNPATTERNSRPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_RPG_Objective_EscortRoot);

	/** Root anchor for "defend target" lifecycle notifications a Defend objective listens to. */
	DESIGNPATTERNSRPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_RPG_Objective_DefendRoot);

	// --- World-hub key roots (children of the WorldHub key namespace) ---

	/** Root key under which "visited stage X of quest Y" flags are stored for prerequisite checks. */
	DESIGNPATTERNSRPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(WorldHub_RPG_StageRoot);

	/** Root key under which lore-unlock flags are stored (replicated + saved by the hub). */
	DESIGNPATTERNSRPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(WorldHub_RPG_LoreRoot);

	// --- Service-locator + persistence keys ---

	/** Service key under which an objective tracker may register for discovery (optional). */
	DESIGNPATTERNSRPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_RPG_QuestGraph);

	/** Persistence-kind tag for the quest-graph stage save record. */
	DESIGNPATTERNSRPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Persist_RPG_QuestGraph);
}
