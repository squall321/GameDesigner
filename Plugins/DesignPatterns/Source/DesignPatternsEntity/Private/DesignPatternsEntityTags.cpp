// Copyright DesignPatterns plugin. All Rights Reserved.

#include "DesignPatternsEntityTags.h"

namespace EntNativeTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Archetype, "Ent.Archetype",
		"Root for entity archetype identity tags (Ent.Archetype.<Family>.<Thing>).");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Cap, "Ent.Cap",
		"Root for capability ids advertised by IEnt_CapabilityProvider (Ent.Cap.<Domain>.<Cap>).");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State, "Ent.State",
		"Root for transient runtime state flags on an entity (Ent.State.<Flag>).");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Trait, "Ent.Trait",
		"Root for trait-kind ids (Ent.Trait.<Kind>).");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Persist_Entity, "Ent.Persist.Entity",
		"Persistence-kind tag for ISeam_Persistable participants serializing the entity trait set.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Trait_TagSet, "Ent.Trait.TagSet",
		"Trait-kind id for the shipped tag-set trait (UEnt_TagSetTrait).");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Trait_StatBag, "Ent.Trait.StatBag",
		"Trait-kind id for the shipped stat-bag trait (UEnt_StatBagTrait).");
}
