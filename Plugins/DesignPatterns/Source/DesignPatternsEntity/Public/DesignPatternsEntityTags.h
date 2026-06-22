// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

/**
 * Native (C++-defined) anchor tags for the DesignPatternsEntity module.
 *
 * These are ROOT/anchor tags only — concrete archetypes, capabilities, states and traits are
 * authored by the game project (or a genre module) as child tags. Anchoring the roots here
 * guarantees the hierarchy exists at startup so tag-hierarchy matching always works
 * (e.g. a query for "Ent.Cap" matches a registered "Ent.Cap.Inventory.Container").
 *
 * Naming convention:
 *   Ent.Archetype.*  — identity buckets for UEnt_ArchetypeAsset::DataTag (what a thing IS).
 *   Ent.Cap.*        — capability ids surfaced through IEnt_CapabilityProvider (what a thing CAN do).
 *   Ent.State.*      — transient runtime state flags carried on the entity component.
 *   Ent.Trait.*      — trait-kind ids for UEnt_Trait::CapabilityTag (which trait authored a capability).
 *
 * The full tag strings are defined in DesignPatternsEntityTags.cpp.
 */
namespace EntNativeTags
{
	/** Root for entity archetype identity tags (Ent.Archetype.<Family>.<Thing>). */
	DESIGNPATTERNSENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Archetype);

	/** Root for capability ids advertised by IEnt_CapabilityProvider (Ent.Cap.<Domain>.<Cap>). */
	DESIGNPATTERNSENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cap);

	/** Root for transient runtime state flags on an entity (Ent.State.<Flag>). */
	DESIGNPATTERNSENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State);

	/** Root for trait-kind ids (Ent.Trait.<Kind>) — used as UEnt_Trait::CapabilityTag defaults. */
	DESIGNPATTERNSENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Trait);

	/** Persistence-kind tag returned by ISeam_Persistable participants that serialize the trait set. */
	DESIGNPATTERNSENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Persist_Entity);

	/** Concrete trait-kind id for the shipped tag-set trait (UEnt_TagSetTrait). */
	DESIGNPATTERNSENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Trait_TagSet);

	/** Concrete trait-kind id for the shipped stat-bag trait (UEnt_StatBagTrait). */
	DESIGNPATTERNSENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Trait_StatBag);

	// ---- Additive deepening anchors (relationship / tags / snapshot / significance) ------------

	/**
	 * Root for entity-relationship link kinds (Ent.Link.<Kind>). These are the FGameplayTag link
	 * kinds carried across ISeam_EntityRelationshipRead and stored on UEnt_RelationshipComponent.
	 * The four shipped kinds below anchor the common topology; projects add their own children.
	 */
	DESIGNPATTERNSENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Link);

	/** One-to-one ownership link (the owner-chain / lifetime-propagation kind): Ent.Link.Owner. */
	DESIGNPATTERNSENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Link_Owner);

	/** Scene/hierarchy parent link (a child belongs under a parent): Ent.Link.Parent. */
	DESIGNPATTERNSENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Link_Parent);

	/** Physical/logical attachment link (e.g. a weapon attached to a socket): Ent.Link.Attached. */
	DESIGNPATTERNSENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Link_Attached);

	/** Group-membership link (squad/party/pack): Ent.Link.Grouped. */
	DESIGNPATTERNSENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Link_Grouped);

	/** Root for the replicated entity tag-container tags (Ent.Tag.<...>) carried on the tag component. */
	DESIGNPATTERNSENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cap_Tags);

	/** Persistence-kind tag for the entity snapshot/rewind subsystem records. */
	DESIGNPATTERNSENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Persist_Snapshot);

	/** Message-bus channel: an entity's relationship links changed (local re-broadcast). */
	DESIGNPATTERNSENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_LinksChanged);

	/** Message-bus channel: an entity's replicated tag set changed (local re-broadcast). */
	DESIGNPATTERNSENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_TagsChanged);

	/** Message-bus channel: an entity's significance bucket changed (local re-broadcast). */
	DESIGNPATTERNSENTITY_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_SignificanceChanged);
}
