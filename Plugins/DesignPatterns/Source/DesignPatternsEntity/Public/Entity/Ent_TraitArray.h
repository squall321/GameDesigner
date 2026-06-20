// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "GameplayTagContainer.h"
#include "Net/Seam_NetValue.h"
#include "Ent_TraitArray.generated.h"

class UEnt_EntityComponent;

/**
 * One replicated trait entry on an entity's trait spine.
 *
 * A trait's *behaviour* lives in a UEnt_Trait instanced subobject (owned by the entity
 * component and reconstructed from the archetype on authority). That live object is NOT
 * replicated directly; instead this lightweight, fully value-typed entry replicates the
 * minimum a client needs to know a trait exists and to mirror its small, net-relevant state:
 *
 *   - TraitClassTag        : the trait-kind id (UEnt_Trait::TraitClassTag) so a client can map
 *                            the entry back to the correct trait class via the archetype/registry.
 *   - CapabilityTag        : the primary capability this trait authors (UEnt_Trait::CapabilityTag).
 *   - ProvidedCapabilities : the full capability set the trait advertises, so IEnt_CapabilityProvider
 *                            queries answer correctly on clients without the live trait object.
 *   - StatePayload         : a single net-friendly value variant (FSeam_NetValue) carrying the
 *                            trait's one replicated scalar/flag/tag, if any. Arbitrary save-only
 *                            state stays inside the live trait object and travels via the save path,
 *                            never here (a raw FInstancedStruct must never be a plain replicated
 *                            UPROPERTY — only FSeam_NetValue is allowed across the wire).
 *
 * Because this is an FFastArraySerializerItem, adds/removes/changes delta-replicate per entry
 * rather than resending the whole array. The Post/PreReplicated* callbacks run on clients only
 * and notify the owning component so it can rebuild/refresh its live trait mirror.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSENTITY_API FEnt_TraitEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	/** Trait-kind id (matches UEnt_Trait::TraitClassTag). Identifies which trait class authored this entry. */
	UPROPERTY(BlueprintReadOnly, Category = "Entity|Trait")
	FGameplayTag TraitClassTag;

	/** Primary capability authored by this trait (matches UEnt_Trait::CapabilityTag). May be empty. */
	UPROPERTY(BlueprintReadOnly, Category = "Entity|Trait")
	FGameplayTag CapabilityTag;

	/** Full set of capabilities advertised by this trait, so client-side capability queries are correct. */
	UPROPERTY(BlueprintReadOnly, Category = "Entity|Trait")
	FGameplayTagContainer ProvidedCapabilities;

	/**
	 * The trait's single net-relevant value (bool/int/float/vector/tag/name), if the trait exposes one.
	 * Unset (Type == None) when the trait has no replicated scalar state. Larger or save-only state
	 * lives on the live trait object and is serialized through the save path, never replicated here.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Entity|Trait")
	FSeam_NetValue StatePayload;

	FEnt_TraitEntry() = default;

	/** True when this entry refers to a real trait kind. */
	bool IsValidEntry() const { return TraitClassTag.IsValid(); }

	//~ FFastArraySerializerItem replication callbacks (invoked on clients only) -----------------

	/** Client: a trait entry is about to be removed — refresh the owning component's live mirror. */
	void PreReplicatedRemove(const struct FEnt_TraitArray& InArraySerializer);

	/** Client: a trait entry was just added — rebuild the corresponding live trait on the component. */
	void PostReplicatedAdd(const struct FEnt_TraitArray& InArraySerializer);

	/** Client: a trait entry's payload changed — push the new value into the live trait. */
	void PostReplicatedChange(const struct FEnt_TraitArray& InArraySerializer);
};

/**
 * Fast-array serializer holding an entity's replicated trait entries.
 *
 * NetDeltaSerialize forwards to FastArrayDeltaSerialize so only changed entries cross the wire.
 * The owning component back-pointer is non-replicated and wired in the component ctor (so it is
 * valid on both server and clients) — the per-entry callbacks use it to notify the component.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSENTITY_API FEnt_TraitArray : public FFastArraySerializer
{
	GENERATED_BODY()

	/** The replicated trait entries. */
	UPROPERTY(BlueprintReadOnly, Category = "Entity|Trait")
	TArray<FEnt_TraitEntry> Entries;

	/** Non-replicated, transient back-pointer to the owning entity component, for change notifications. */
	UPROPERTY(NotReplicated, Transient)
	TObjectPtr<UEnt_EntityComponent> OwnerComponent = nullptr;

	/** Delta-serialize only the changed entries. */
	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FastArrayDeltaSerialize<FEnt_TraitEntry, FEnt_TraitArray>(Entries, DeltaParms, *this);
	}

	/** Find an entry by its trait-kind tag, or null if absent. */
	FEnt_TraitEntry* FindByTraitClassTag(const FGameplayTag& InTraitClassTag)
	{
		for (FEnt_TraitEntry& Entry : Entries)
		{
			if (Entry.TraitClassTag == InTraitClassTag)
			{
				return &Entry;
			}
		}
		return nullptr;
	}

	/** Const overload of FindByTraitClassTag. */
	const FEnt_TraitEntry* FindByTraitClassTag(const FGameplayTag& InTraitClassTag) const
	{
		for (const FEnt_TraitEntry& Entry : Entries)
		{
			if (Entry.TraitClassTag == InTraitClassTag)
			{
				return &Entry;
			}
		}
		return nullptr;
	}
};

/** Enables NetDeltaSerialize for the trait array. */
template<>
struct TStructOpsTypeTraits<FEnt_TraitArray> : public TStructOpsTypeTraitsBase2<FEnt_TraitArray>
{
	enum { WithNetDeltaSerializer = true };
};
