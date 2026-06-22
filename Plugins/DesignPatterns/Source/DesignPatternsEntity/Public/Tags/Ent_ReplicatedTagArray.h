// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "GameplayTagContainer.h"
#include "Net/Seam_NetValue.h"
#include "Ent_ReplicatedTagArray.generated.h"

class UEnt_TagContainerComponent;

/**
 * One replicated gameplay-tag entry carrying an optional stack count.
 *
 * Mirrors the FEnt_TraitEntry pattern: an FFastArraySerializerItem so add/remove/change
 * delta-replicate per entry. The stack count rides an FSeam_NetValue (never a raw replicated
 * FInstancedStruct). Pre/PostReplicated* run on CLIENTS ONLY and notify the owning component.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSENTITY_API FEnt_ReplicatedTag : public FFastArraySerializerItem
{
	GENERATED_BODY()

	/** The gameplay tag this entry represents (typically under Ent.Tag.*). */
	UPROPERTY(BlueprintReadOnly, Category = "Entity|Tag")
	FGameplayTag Tag;

	/** Stack count for the tag (Int net-value). One when the tag is simply present. */
	UPROPERTY(BlueprintReadOnly, Category = "Entity|Tag")
	FSeam_NetValue StackCount;

	FEnt_ReplicatedTag() = default;

	FEnt_ReplicatedTag(const FGameplayTag& InTag, int32 InCount)
		: Tag(InTag)
		, StackCount(FSeam_NetValue::MakeInt(InCount))
	{
	}

	/** Current stack count as an int (1 when unset). */
	int32 GetCount() const { return StackCount.IsSet() ? static_cast<int32>(StackCount.IntValue) : 1; }

	bool IsValidEntry() const { return Tag.IsValid(); }

	//~ FFastArraySerializerItem replication callbacks (clients only) ----------------------------
	void PreReplicatedRemove(const struct FEnt_ReplicatedTagArray& InArraySerializer);
	void PostReplicatedAdd(const struct FEnt_ReplicatedTagArray& InArraySerializer);
	void PostReplicatedChange(const struct FEnt_ReplicatedTagArray& InArraySerializer);
};

/**
 * Fast-array serializer holding an entity's replicated gameplay tags.
 *
 * NetDeltaSerialize forwards to FastArrayDeltaSerialize so only changed entries cross the wire. The
 * owning component back-pointer is non-replicated and wired in the component ctor.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSENTITY_API FEnt_ReplicatedTagArray : public FFastArraySerializer
{
	GENERATED_BODY()

	/** The replicated tag entries. */
	UPROPERTY(BlueprintReadOnly, Category = "Entity|Tag")
	TArray<FEnt_ReplicatedTag> Entries;

	/** Non-replicated, transient back-pointer to the owning tag component. */
	UPROPERTY(NotReplicated, Transient)
	TObjectPtr<UEnt_TagContainerComponent> OwnerComponent = nullptr;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FastArrayDeltaSerialize<FEnt_ReplicatedTag, FEnt_ReplicatedTagArray>(Entries, DeltaParms, *this);
	}

	/** Find an entry by exact tag, or null. */
	FEnt_ReplicatedTag* FindByTag(const FGameplayTag& InTag)
	{
		for (FEnt_ReplicatedTag& Entry : Entries)
		{
			if (Entry.Tag.MatchesTagExact(InTag))
			{
				return &Entry;
			}
		}
		return nullptr;
	}

	/** Const overload of FindByTag. */
	const FEnt_ReplicatedTag* FindByTag(const FGameplayTag& InTag) const
	{
		for (const FEnt_ReplicatedTag& Entry : Entries)
		{
			if (Entry.Tag.MatchesTagExact(InTag))
			{
				return &Entry;
			}
		}
		return nullptr;
	}
};

/** Enables NetDeltaSerialize for the tag array. */
template<>
struct TStructOpsTypeTraits<FEnt_ReplicatedTagArray> : public TStructOpsTypeTraitsBase2<FEnt_ReplicatedTagArray>
{
	enum { WithNetDeltaSerializer = true };
};
