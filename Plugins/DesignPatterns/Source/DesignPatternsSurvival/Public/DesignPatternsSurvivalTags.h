// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

/**
 * Native (C++-defined) anchor tags for the DesignPatternsSurvival module.
 *
 * These are ROOT/anchor tags only — concrete resources, needs and stations are authored by the
 * game project as child tags. Anchoring the roots here guarantees the hierarchy exists at
 * startup so tag-hierarchy matching always works (e.g. a station tag "Surv.Station.Forge"
 * matches a recipe requiring "Surv.Station").
 */
namespace SurvNativeTags
{
	/** Root for harvestable resource identities (e.g. Surv.Resource.Wood). */
	DESIGNPATTERNSSURVIVAL_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Resource);

	/** Root for survival needs (Surv.Need.Hunger / Thirst / Stamina / Temperature). */
	DESIGNPATTERNSSURVIVAL_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Need);

	/** Root for crafting-station kinds (Surv.Station.Workbench, Surv.Station.Forge...). */
	DESIGNPATTERNSSURVIVAL_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Station);

	/** Root for message-bus channels broadcast by this module (children of DP.Bus by convention). */
	DESIGNPATTERNSSURVIVAL_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus);

	// ---- Additive deepening roots (crafting depth + building) ----

	/** Root for tech-tree nodes / unlocks (e.g. Surv.Tech.Smithing). Authored by the project as children. */
	DESIGNPATTERNSSURVIVAL_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Tech);

	/** Root for buildable / building-piece identities and structural roles (e.g. Surv.Build.Foundation). */
	DESIGNPATTERNSSURVIVAL_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Build);

	/** Root for snap-socket kinds used to join building pieces (e.g. Surv.Build.SocketType.Floor). */
	DESIGNPATTERNSSURVIVAL_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(BuildSocketType);

	/** Root for craft quality tiers granted by advanced crafting (e.g. Surv.Quality.Fine / Masterwork). */
	DESIGNPATTERNSSURVIVAL_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Quality);

	// ---- Bus channels (CHILDREN of the existing Survival bus anchor, never a parallel root) ----

	/** Broadcast (locally, server + clients) when a building piece is committed. Payload = piece info. */
	DESIGNPATTERNSSURVIVAL_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_BuildPlaced);

	/** Broadcast when a building piece is removed or collapses. */
	DESIGNPATTERNSSURVIVAL_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_BuildRemoved);

	/** Broadcast when a building piece loses or regains structural support. */
	DESIGNPATTERNSSURVIVAL_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_BuildSupportChanged);

	/** Broadcast when a tech node is researched / recipe discovered (knowledge ledger changed). */
	DESIGNPATTERNSSURVIVAL_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_KnowledgeChanged);

	/** Broadcast (cosmetic) when an advanced craft rolls a critical / extra-quality result. */
	DESIGNPATTERNSSURVIVAL_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_CraftCritical);

	// ---- Service-locator-style well-known process tags (defensive defaults; projects may add more) ----

	/** Persistence kind tag for the per-player knowledge ledger (routes save records). */
	DESIGNPATTERNSSURVIVAL_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Persist_Knowledge);

	/** Persistence kind tag for a placed building piece. */
	DESIGNPATTERNSSURVIVAL_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Persist_Structure);
}
