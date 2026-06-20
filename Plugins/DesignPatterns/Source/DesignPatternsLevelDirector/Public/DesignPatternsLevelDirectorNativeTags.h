// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

/**
 * Native (C++-defined) anchor tags for the DesignPatternsLevelDirector module's PLACEMENT area.
 *
 * Only ROOT/anchor tags are declared in native code so the hierarchy is guaranteed to exist at
 * startup (the message bus' tag-hierarchy matching relies on the parents existing). Concrete leaf
 * ids — individual region tags, tile-type tags, actor-class identity tags — are authored by the
 * GAME project as child tags in its tag config; this module never bakes gameplay-specific leaves.
 *
 * Message-bus channels (children of the core DP.Bus root, so a listener on DP.Bus still hears them):
 *   DP.Bus.Lvl.Placement.Generated — a placement pass produced a manifest (payload FLvl_PlacementEventPayload).
 *   DP.Bus.Lvl.Placement.Cleared   — a placement pass was torn down.
 *   DP.Bus.Lvl.Encounter.Activated / .Deactivated — a region's encounter (de)activated by the activator.
 *
 * Service-locator keys (children of the core DP.Service root):
 *   DP.Service.Lvl.ActivationGate — OPTIONAL: a World-side adapter registers an ISeam_ActivationGate
 *                                   here. Unresolved -> gate defaults OPEN (content active).
 *   DP.Service.Lvl.TileProvider   — OPTIONAL: the SimGrid adapter registers an ISeam_TileProviderRead
 *                                   here. Unresolved -> placement skips tile-mask validation (passes).
 */
namespace LvlNativeTags
{
	// ---- Message-bus channel anchors (children of the core DP.Bus root) ----

	/** A procedural placement pass produced a manifest. */
	DESIGNPATTERNSLEVELDIRECTOR_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Lvl_Placement_Generated);

	/** A procedural placement pass was cleared / torn down. */
	DESIGNPATTERNSLEVELDIRECTOR_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Lvl_Placement_Cleared);

	/** A region's encounter became active (gate open + interest present). */
	DESIGNPATTERNSLEVELDIRECTOR_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Lvl_Encounter_Activated);

	/** A region's encounter became inactive (gate closed or interest left). */
	DESIGNPATTERNSLEVELDIRECTOR_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Lvl_Encounter_Deactivated);

	// ---- Service-locator key anchors (children of the core DP.Service root) ----

	/** Key under which a World-side adapter registers an ISeam_ActivationGate (optional; default open). */
	DESIGNPATTERNSLEVELDIRECTOR_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_Lvl_ActivationGate);

	/** Key under which a grid adapter registers an ISeam_TileProviderRead (optional; default pass). */
	DESIGNPATTERNSLEVELDIRECTOR_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_Lvl_TileProvider);

	// ---- Persistence record-kind anchors (children of the core DP.Persist root) ----

	/** Persistence kind of a procedural placer's manifest record (ISeam_Persistable::GetPersistenceKind). */
	DESIGNPATTERNSLEVELDIRECTOR_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Persist_Lvl_Placement);
}
