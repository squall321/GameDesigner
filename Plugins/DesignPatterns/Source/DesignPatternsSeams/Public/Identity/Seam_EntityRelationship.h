// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Identity/Seam_EntityId.h"

/**
 * Cross-cutting READ seam over the Entity module's relationship index.
 *
 * Lets AI, quest, save and tooling systems ask "which entities are linked to this one, and how"
 * by stable FSeam_EntityId WITHOUT including the Entity module's concrete component/subsystem
 * headers. Implemented by UEnt_RelationshipSubsystem (a world subsystem), resolved by a consumer
 * casting the world subsystem to this interface (or via the service locator).
 *
 * HOUSE STYLE — raw C++ virtuals (sibling to ISeam_Reputation / ISeam_FactionStanding), NOT
 * BlueprintNativeEvent: this is an internal read seam over a framework subsystem, not a
 * project-supplied bridge or a UI-facing contract. Keeping it raw-virtual means there is no
 * UInterface UClass and no generated thunks — the implementer simply derives from this and the
 * consumer does `Cast<ISeam_EntityRelationshipRead>(WorldSubsystem)`.
 *
 * LINK KINDS ARE TAGS — there is deliberately NO ESeam_EntityLinkKind enum. Every link kind is an
 * FGameplayTag under the Entity module's `Ent.Link.*` anchor hierarchy (e.g. Ent.Link.Owner,
 * Ent.Link.Parent, Ent.Link.Attached, Ent.Link.Grouped). This matches the framework's
 * tag-everywhere rule and avoids two competing identity schemes crossing the seam.
 *
 * THREAD/AUTHORITY — all methods are const reads and are safe on server and clients (the index is
 * built locally on every machine from replicated relationship components, like the entity
 * registry). They never mutate game state and carry no authority requirement.
 */
class DESIGNPATTERNSSEAMS_API ISeam_EntityRelationshipRead
{
public:
	virtual ~ISeam_EntityRelationshipRead() = default;

	/**
	 * True when a relationship index is actually present on this machine. Lets a consumer
	 * distinguish "no index / Entity module absent" from "this entity simply has no links".
	 */
	virtual bool HasRelationshipIndex() const = 0;

	/**
	 * Append the targets of every outgoing link of kind LinkKindTag from Source into Out.
	 *
	 * Exact-tag semantics on LinkKindTag by default. Passing an invalid (empty) LinkKindTag returns
	 * the targets of ALL outgoing links from Source regardless of kind. Out is appended to (never
	 * reset) so callers can aggregate across several sources; duplicates are not de-duplicated by
	 * the seam. Returns the number of ids appended.
	 */
	virtual int32 GetLinkedEntities(FSeam_EntityId Source, FGameplayTag LinkKindTag, TArray<FSeam_EntityId>& Out) const = 0;

	/**
	 * The primary owner of Source — the target of its single owner-kind link (Ent.Link.Owner), or an
	 * invalid id when Source has no owner / is itself a root. Distinct from generic links because
	 * ownership is the one-to-one relationship used for lifetime propagation and owner-chain walks.
	 */
	virtual FSeam_EntityId GetPrimaryOwner(FSeam_EntityId Source) const = 0;

	/**
	 * True if A has an outgoing link to B of kind LinkKindTag. With an invalid LinkKindTag, true if A
	 * has ANY outgoing link to B. This is directional (A->B); callers wanting symmetry should test
	 * both orders.
	 */
	virtual bool AreLinked(FSeam_EntityId A, FSeam_EntityId B, FGameplayTag LinkKindTag) const = 0;
};
