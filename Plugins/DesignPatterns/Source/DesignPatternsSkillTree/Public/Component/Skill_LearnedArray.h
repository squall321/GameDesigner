// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "GameplayTagContainer.h"
#include "Skill_LearnedArray.generated.h"

class USkill_SkillComponent;

/**
 * One replicated "learned skill" entry on a character's skill spine.
 *
 * A skill's *definition* (prerequisites, mutex group, tier, max rank, per-rank cost, linked ability)
 * lives in the SkillTree data assets owned by the data+seams area — those are designer authored and
 * never replicated. This lightweight, fully value-typed entry replicates the minimum a client needs:
 * which skill is learned and at what rank, so UI and gameplay reads are correct on simulated proxies
 * without sending any definition data.
 *
 *   - SkillTag : the skill id (matches the data asset's SkillTag), used to map the entry back to its
 *                definition via the SkillTree data registry/library.
 *   - Rank     : the current learned rank (>= 1 once learned; never replicated below 1, removed instead).
 *
 * Because this is an FFastArraySerializerItem, learns/respecs/rank-ups delta-replicate per entry rather
 * than resending the whole array. The Pre/PostReplicated* callbacks run on clients only and notify the
 * owning component so it can fire local change delegates and refresh any derived view state.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSKILLTREE_API FSkill_LearnedEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	/** The learned skill's id (matches the skill definition asset's SkillTag). */
	UPROPERTY(BlueprintReadOnly, Category = "SkillTree|Learned")
	FGameplayTag SkillTag;

	/** The current learned rank. >= 1 while present; a fully un-learned skill is removed, not kept at 0. */
	UPROPERTY(BlueprintReadOnly, Category = "SkillTree|Learned")
	int32 Rank = 0;

	FSkill_LearnedEntry() = default;

	/** Convenience constructor. */
	FSkill_LearnedEntry(const FGameplayTag& InSkillTag, int32 InRank)
		: SkillTag(InSkillTag), Rank(InRank) {}

	/** True when this entry refers to a real, learned skill. */
	bool IsValidEntry() const { return SkillTag.IsValid() && Rank > 0; }

	//~ FFastArraySerializerItem replication callbacks (invoked on clients only) -----------------

	/** Client: a learned-skill entry is about to be removed (respec) — notify the owning component. */
	void PreReplicatedRemove(const struct FSkill_LearnedArray& InArraySerializer);

	/** Client: a learned-skill entry was just added (learn) — notify the owning component. */
	void PostReplicatedAdd(const struct FSkill_LearnedArray& InArraySerializer);

	/** Client: a learned-skill entry's rank changed (rank-up) — notify the owning component. */
	void PostReplicatedChange(const struct FSkill_LearnedArray& InArraySerializer);
};

/**
 * Fast-array serializer holding a character's replicated learned-skill entries.
 *
 * NetDeltaSerialize forwards to FastArrayDeltaSerialize so only changed entries cross the wire.
 * The owning component back-pointer is non-replicated and wired in the component ctor (so it is valid
 * on both server and clients) — the per-entry callbacks use it to fire change notifications. The
 * back-pointer is Transient/NotReplicated so it is never serialized or sent across the network.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSKILLTREE_API FSkill_LearnedArray : public FFastArraySerializer
{
	GENERATED_BODY()

	/** The replicated learned-skill entries. */
	UPROPERTY(BlueprintReadOnly, Category = "SkillTree|Learned")
	TArray<FSkill_LearnedEntry> Entries;

	/** Non-replicated, transient back-pointer to the owning skill component, for change notifications. */
	UPROPERTY(NotReplicated, Transient)
	TObjectPtr<USkill_SkillComponent> OwnerComponent = nullptr;

	/** Delta-serialize only the changed entries. */
	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FastArrayDeltaSerialize<FSkill_LearnedEntry, FSkill_LearnedArray>(Entries, DeltaParms, *this);
	}

	/** Find an entry by skill tag (mutable), or null if absent. */
	FSkill_LearnedEntry* FindBySkillTag(const FGameplayTag& InSkillTag)
	{
		for (FSkill_LearnedEntry& Entry : Entries)
		{
			if (Entry.SkillTag == InSkillTag)
			{
				return &Entry;
			}
		}
		return nullptr;
	}

	/** Const overload of FindBySkillTag. */
	const FSkill_LearnedEntry* FindBySkillTag(const FGameplayTag& InSkillTag) const
	{
		for (const FSkill_LearnedEntry& Entry : Entries)
		{
			if (Entry.SkillTag == InSkillTag)
			{
				return &Entry;
			}
		}
		return nullptr;
	}
};

/** Enables NetDeltaSerialize for the learned-skill array. */
template<>
struct TStructOpsTypeTraits<FSkill_LearnedArray> : public TStructOpsTypeTraitsBase2<FSkill_LearnedArray>
{
	enum { WithNetDeltaSerializer = true };
};
