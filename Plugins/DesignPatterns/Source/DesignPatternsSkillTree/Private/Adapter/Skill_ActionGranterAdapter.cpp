// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Adapter/Skill_ActionGranterAdapter.h"
#include "Action/DPGameplayActionComponent.h"
#include "Action/DPGameplayActionLite.h"
#include "Core/DPLog.h"

#include "GameFramework/Actor.h"

USkill_ActionGranterAdapter::USkill_ActionGranterAdapter()
{
	PrimaryComponentTick.bCanEverTick = false;

	// Holds no replicated state of its own — the action component it forwards to owns the authoritative
	// grant state — so this adapter does not need to replicate. It simply translates seam calls into
	// UDP_GameplayActionComponent grants/removes on the authority.
}

// ---- Authority ---------------------------------------------------------------------------------

bool USkill_ActionGranterAdapter::HasAuthorityToMutate() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}

// ---- Backend resolution ------------------------------------------------------------------------

UDP_GameplayActionComponent* USkill_ActionGranterAdapter::ResolveActionComponent() const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}
	// The action backend we forward grants to. Expected to live on the same actor as this adapter and the
	// skill component. May legitimately be null on a passive-only actor — callers treat that as a skipped grant.
	return Owner->FindComponentByClass<UDP_GameplayActionComponent>();
}

FSkill_GrantedAbilityRecord* USkill_ActionGranterAdapter::FindRecord(const FGameplayTag& AbilityTag, const FGameplayTag& SourceTag)
{
	for (FSkill_GrantedAbilityRecord& Record : GrantedRecords)
	{
		if (Record.AbilityTag == AbilityTag && Record.SourceTag == SourceTag)
		{
			return &Record;
		}
	}
	return nullptr;
}

// ---- ISkill_AbilityGranter ---------------------------------------------------------------------

bool USkill_ActionGranterAdapter::GrantAbility_Implementation(FGameplayTag AbilityTag, int32 Rank, FGameplayTag SourceTag)
{
	// Seam contract: GrantAbility mutates gameplay state and is authority-only. Guard defensively even though
	// the progression component already authority-checked before invoking us.
	if (!HasAuthorityToMutate())
	{
		UE_LOG(LogDP, Verbose, TEXT("[SkillTree] GrantAbility('%s') ignored on non-authority."), *AbilityTag.ToString());
		return false;
	}

	if (!AbilityTag.IsValid())
	{
		return false;
	}

	// Already granted for this exact (ability, source)? Treat a same-or-lower rank re-grant as an idempotent
	// success (the lightweight action system is rank-agnostic; one grant per source suffices). Record the
	// highest rank seen for accurate save/debug.
	if (FSkill_GrantedAbilityRecord* Existing = FindRecord(AbilityTag, SourceTag))
	{
		Existing->GrantedRank = FMath::Max(Existing->GrantedRank, Rank);
		return Existing->Handle.IsValid();
	}

	// Map the ability tag to a concrete action class. An unmapped tag means "no concrete action": log and
	// return false so the skill learn treats it as a skipped-grant degrade (the rank is still recorded).
	const TSoftClassPtr<UDP_GameplayActionLite>* SoftClassPtr = AbilityClassMap.Find(AbilityTag);
	if (!SoftClassPtr || SoftClassPtr->IsNull())
	{
		UE_LOG(LogDP, Verbose, TEXT("[SkillTree] No action class mapped for ability '%s'; skipping grant."),
			*AbilityTag.ToString());
		return false;
	}

	UDP_GameplayActionComponent* ActionComp = ResolveActionComponent();
	if (!ActionComp)
	{
		UE_LOG(LogDP, Warning, TEXT("[SkillTree] No UDP_GameplayActionComponent on '%s'; cannot grant ability '%s'."),
			*GetNameSafe(GetOwner()), *AbilityTag.ToString());
		return false;
	}

	// Sync-load the soft action class (only when a skill that grants it is actually learned).
	UClass* ActionClass = SoftClassPtr->LoadSynchronous();
	if (!ActionClass)
	{
		UE_LOG(LogDP, Warning, TEXT("[SkillTree] Failed to load action class for ability '%s'."), *AbilityTag.ToString());
		return false;
	}

	const FDP_ActionSpecHandle Handle = ActionComp->GrantAction(ActionClass);
	if (!Handle.IsValid())
	{
		UE_LOG(LogDP, Warning, TEXT("[SkillTree] GrantAction returned an invalid handle for ability '%s'."), *AbilityTag.ToString());
		return false;
	}

	FSkill_GrantedAbilityRecord& NewRecord = GrantedRecords.AddDefaulted_GetRef();
	NewRecord.AbilityTag = AbilityTag;
	NewRecord.SourceTag = SourceTag;
	NewRecord.GrantedRank = FMath::Max(0, Rank);
	NewRecord.Handle = Handle;

	UE_LOG(LogDP, Verbose, TEXT("[SkillTree] Granted ability '%s' (source '%s', %s)."),
		*AbilityTag.ToString(), *SourceTag.ToString(), *Handle.ToString());
	return true;
}

bool USkill_ActionGranterAdapter::RevokeAbility_Implementation(FGameplayTag AbilityTag, FGameplayTag SourceTag)
{
	if (!HasAuthorityToMutate())
	{
		UE_LOG(LogDP, Verbose, TEXT("[SkillTree] RevokeAbility('%s') ignored on non-authority."), *AbilityTag.ToString());
		return false;
	}

	// Only remove a grant whose (ability, source) matches, so two nodes granting the same ability tag don't
	// clobber each other (per the seam contract).
	const int32 Index = GrantedRecords.IndexOfByPredicate(
		[&AbilityTag, &SourceTag](const FSkill_GrantedAbilityRecord& R)
		{
			return R.AbilityTag == AbilityTag && R.SourceTag == SourceTag;
		});

	if (Index == INDEX_NONE)
	{
		return false;
	}

	const FDP_ActionSpecHandle Handle = GrantedRecords[Index].Handle;
	GrantedRecords.RemoveAt(Index);

	bool bRemoved = false;
	if (Handle.IsValid())
	{
		if (UDP_GameplayActionComponent* ActionComp = ResolveActionComponent())
		{
			bRemoved = ActionComp->RemoveAction(Handle);
		}
	}

	UE_LOG(LogDP, Verbose, TEXT("[SkillTree] Revoked ability '%s' (source '%s') -> %s."),
		*AbilityTag.ToString(), *SourceTag.ToString(), bRemoved ? TEXT("removed") : TEXT("no-op"));
	return bRemoved;
}

bool USkill_ActionGranterAdapter::HasAbility_Implementation(FGameplayTag AbilityTag) const
{
	// Read-only: ask the action backend whether the tag is among its granted actions. Works on clients too,
	// since UDP_GameplayActionComponent replicates its granted-action tag list.
	if (const UDP_GameplayActionComponent* ActionComp = ResolveActionComponent())
	{
		return ActionComp->HasActionWithTag(AbilityTag);
	}
	return false;
}
