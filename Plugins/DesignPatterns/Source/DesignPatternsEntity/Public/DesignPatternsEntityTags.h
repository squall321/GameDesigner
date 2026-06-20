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
}
