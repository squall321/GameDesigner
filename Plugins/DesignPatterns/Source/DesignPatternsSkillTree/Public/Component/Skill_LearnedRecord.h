// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Skill_LearnedRecord.generated.h"

/**
 * A plain, value-typed record of one learned skill, used for save/load.
 *
 * Distinct from FSkill_LearnedEntry (the replicated fast-array item) because save data must NOT carry
 * the FFastArraySerializerItem replication bookkeeping (ReplicationID/ReplicationKey/MostRecentArrayReplicationKey).
 * This struct is just the durable facts — which skill and at what rank — and is what the
 * USkill_SkillSaveGame serializes via the standard SaveGame UPROPERTY path.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSKILLTREE_API FSkill_LearnedRecord
{
	GENERATED_BODY()

	/** The learned skill's id (matches the skill definition asset's SkillTag). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "SkillTree|Save")
	FGameplayTag SkillTag;

	/** The learned rank at save time (>= 1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "SkillTree|Save")
	int32 Rank = 0;

	FSkill_LearnedRecord() = default;

	/** Convenience constructor. */
	FSkill_LearnedRecord(const FGameplayTag& InSkillTag, int32 InRank)
		: SkillTag(InSkillTag), Rank(InRank) {}

	/** True when this record refers to a real, learned skill. */
	bool IsValidRecord() const { return SkillTag.IsValid() && Rank > 0; }
};
