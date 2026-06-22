// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "GameplayTagContainer.h"
#include "Identity/Seam_EntityId.h"
#include "Net/Seam_NetValue.h"
#include "Ent_EntityLinkArray.generated.h"

class UEnt_RelationshipComponent;

/**
 * A plain (non-replicated, non-fast-array) directed link record, used for SAVE and SNAPSHOT.
 *
 * The replicated form is FEnt_EntityLink (an FFastArraySerializerItem); a saved/snapshotted form must
 * be a plain value struct (a fast-array item must never appear inside a save struct). FEnt_EntityLink
 * exposes ToRecord() to flatten itself into this shape for the snapshot subsystem.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSENTITY_API FEnt_LinkRecord
{
	GENERATED_BODY()

	/** The entity this link points AT (the link is directed Source -> TargetId). */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Entity|Link")
	FSeam_EntityId TargetId;

	/** The link kind (Ent.Link.<Kind>). */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Entity|Link")
	FGameplayTag LinkKindTag;

	/**
	 * Optional net-friendly scalar parameter for the link (e.g. a slot index for an attachment, a
	 * group role). Unset (Type == None) when the link carries no parameter.
	 */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Entity|Link")
	FSeam_NetValue Param;

	FEnt_LinkRecord() = default;

	FEnt_LinkRecord(const FSeam_EntityId& InTarget, const FGameplayTag& InKind, const FSeam_NetValue& InParam)
		: TargetId(InTarget)
		, LinkKindTag(InKind)
		, Param(InParam)
	{
	}

	/** True when this record names a real target and kind. */
	bool IsValidRecord() const { return TargetId.IsValid() && LinkKindTag.IsValid(); }
};

/**
 * One replicated outgoing link entry on an entity's relationship spine.
 *
 * Mirrors FEnt_TraitEntry exactly: it is an FFastArraySerializerItem, so adds/removes/changes
 * delta-replicate per entry rather than resending the whole array. The Pre/PostReplicated* callbacks
 * run on CLIENTS ONLY and notify the owning component so it can refresh the world relationship index
 * and fire OnLinksChanged.
 *
 * The link is directional: the owning component IS the source; this entry names the TargetId, the
 * LinkKindTag and an optional net-friendly Param (FSeam_NetValue — never a raw FInstancedStruct on
 * the wire).
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSENTITY_API FEnt_EntityLink : public FFastArraySerializerItem
{
	GENERATED_BODY()

	/** The entity this link points AT. */
	UPROPERTY(BlueprintReadOnly, Category = "Entity|Link")
	FSeam_EntityId TargetId;

	/** The link kind (Ent.Link.<Kind>). */
	UPROPERTY(BlueprintReadOnly, Category = "Entity|Link")
	FGameplayTag LinkKindTag;

	/** Optional net-friendly scalar parameter. Unset (Type == None) when absent. */
	UPROPERTY(BlueprintReadOnly, Category = "Entity|Link")
	FSeam_NetValue Param;

	FEnt_EntityLink() = default;

	FEnt_EntityLink(const FSeam_EntityId& InTarget, const FGameplayTag& InKind, const FSeam_NetValue& InParam)
		: TargetId(InTarget)
		, LinkKindTag(InKind)
		, Param(InParam)
	{
	}

	/** True when this entry refers to a real target and kind. */
	bool IsValidEntry() const { return TargetId.IsValid() && LinkKindTag.IsValid(); }

	/** Flatten to the plain save/snapshot record form. */
	FEnt_LinkRecord ToRecord() const { return FEnt_LinkRecord(TargetId, LinkKindTag, Param); }

	//~ FFastArraySerializerItem replication callbacks (invoked on clients only) -----------------

	/** Client: a link entry is about to be removed — refresh the owning component's index/mirror. */
	void PreReplicatedRemove(const struct FEnt_EntityLinkArray& InArraySerializer);

	/** Client: a link entry was just added — register it into the world index and notify. */
	void PostReplicatedAdd(const struct FEnt_EntityLinkArray& InArraySerializer);

	/** Client: a link entry's payload changed — re-register and notify. */
	void PostReplicatedChange(const struct FEnt_EntityLinkArray& InArraySerializer);
};

/**
 * Fast-array serializer holding an entity's replicated outgoing links.
 *
 * NetDeltaSerialize forwards to FastArrayDeltaSerialize so only changed entries cross the wire. The
 * owning component back-pointer is non-replicated and wired in the component ctor (valid on server
 * and clients) — the per-entry callbacks use it to notify the component.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSENTITY_API FEnt_EntityLinkArray : public FFastArraySerializer
{
	GENERATED_BODY()

	/** The replicated link entries. */
	UPROPERTY(BlueprintReadOnly, Category = "Entity|Link")
	TArray<FEnt_EntityLink> Links;

	/** Non-replicated, transient back-pointer to the owning relationship component. */
	UPROPERTY(NotReplicated, Transient)
	TObjectPtr<UEnt_RelationshipComponent> OwnerComponent = nullptr;

	/** Delta-serialize only the changed entries. */
	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FastArrayDeltaSerialize<FEnt_EntityLink, FEnt_EntityLinkArray>(Links, DeltaParms, *this);
	}

	/** Find a link entry by target + kind, or null. */
	FEnt_EntityLink* FindLink(const FSeam_EntityId& TargetId, const FGameplayTag& KindTag)
	{
		for (FEnt_EntityLink& Link : Links)
		{
			if (Link.TargetId == TargetId && Link.LinkKindTag == KindTag)
			{
				return &Link;
			}
		}
		return nullptr;
	}

	/** Const overload of FindLink. */
	const FEnt_EntityLink* FindLink(const FSeam_EntityId& TargetId, const FGameplayTag& KindTag) const
	{
		for (const FEnt_EntityLink& Link : Links)
		{
			if (Link.TargetId == TargetId && Link.LinkKindTag == KindTag)
			{
				return &Link;
			}
		}
		return nullptr;
	}
};

/** Enables NetDeltaSerialize for the link array. */
template<>
struct TStructOpsTypeTraits<FEnt_EntityLinkArray> : public TStructOpsTypeTraitsBase2<FEnt_EntityLinkArray>
{
	enum { WithNetDeltaSerializer = true };
};
