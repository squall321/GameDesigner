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

	// ---- Additive deepening anchors ------------------------------------------------------------

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Link, "Ent.Link",
		"Root for entity-relationship link kinds (Ent.Link.<Kind>).");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Link_Owner, "Ent.Link.Owner",
		"One-to-one ownership link (owner-chain / lifetime-propagation kind).");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Link_Parent, "Ent.Link.Parent",
		"Scene/hierarchy parent link (a child belongs under a parent).");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Link_Attached, "Ent.Link.Attached",
		"Physical/logical attachment link (e.g. a weapon attached to a socket).");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Link_Grouped, "Ent.Link.Grouped",
		"Group-membership link (squad/party/pack).");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Cap_Tags, "Ent.Tag",
		"Root for replicated entity tag-container tags carried on UEnt_TagContainerComponent.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Persist_Snapshot, "Ent.Persist.Snapshot",
		"Persistence-kind tag for the entity snapshot/rewind subsystem records.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_LinksChanged, "DP.Bus.Entity.LinksChanged",
		"Message-bus channel: an entity's relationship links changed (local re-broadcast).");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_TagsChanged, "DP.Bus.Entity.TagsChanged",
		"Message-bus channel: an entity's replicated tag set changed (local re-broadcast).");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_SignificanceChanged, "DP.Bus.Entity.SignificanceChanged",
		"Message-bus channel: an entity's significance bucket changed (local re-broadcast).");
}
