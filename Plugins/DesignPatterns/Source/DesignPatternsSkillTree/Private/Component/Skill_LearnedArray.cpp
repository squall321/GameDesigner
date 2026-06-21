// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Component/Skill_LearnedArray.h"
#include "Component/Skill_SkillComponent.h"
#include "Core/DPLog.h"

void FSkill_LearnedEntry::PreReplicatedRemove(const FSkill_LearnedArray& InArraySerializer)
{
	// Client-only: a learned skill is about to disappear (respec). Notify the owning component so it can
	// fire its local point-budget delegate. The back-pointer is wired in the component ctor and so is valid
	// on clients; null-check defensively in case of an out-of-order construction edge.
	if (USkill_SkillComponent* Owner = InArraySerializer.OwnerComponent)
	{
		Owner->NotifyLearnedEntryRemoved(SkillTag);
	}
}

void FSkill_LearnedEntry::PostReplicatedAdd(const FSkill_LearnedArray& InArraySerializer)
{
	// Client-only: a skill was just learned on the server and replicated here. Notify the component so it
	// broadcasts OnSkillLearned / OnPointsChanged locally for UI and cosmetic reactions.
	if (USkill_SkillComponent* Owner = InArraySerializer.OwnerComponent)
	{
		Owner->NotifyLearnedEntryChanged(SkillTag, Rank);
	}
}

void FSkill_LearnedEntry::PostReplicatedChange(const FSkill_LearnedArray& InArraySerializer)
{
	// Client-only: an existing learned skill's rank changed (rank-up). Same notification as add.
	if (USkill_SkillComponent* Owner = InArraySerializer.OwnerComponent)
	{
		Owner->NotifyLearnedEntryChanged(SkillTag, Rank);
	}
}
