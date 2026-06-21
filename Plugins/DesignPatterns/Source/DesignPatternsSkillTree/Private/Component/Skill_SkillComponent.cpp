// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Component/Skill_SkillComponent.h"
#include "Seam/Skill_AbilityGranter.h"
#include "Seam/Skill_PointSource.h"
#include "Skill/Skill_SkillDefinition.h"
#include "Skill/Skill_SkillTreeDefinition.h"
#include "Skill/Skill_Types.h"
#include "Settings/Skill_DeveloperSettings.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Data/DPDataRegistrySubsystem.h"

#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"

// ---- Native failure-reason tags ----------------------------------------------------------------

namespace SkillFailureTags
{
	UE_DEFINE_GAMEPLAY_TAG(Failure_Unknown, "Skill.Failure.Unknown");
	UE_DEFINE_GAMEPLAY_TAG(Failure_MaxRank, "Skill.Failure.MaxRank");
	UE_DEFINE_GAMEPLAY_TAG(Failure_Prereq,  "Skill.Failure.Prereq");
	UE_DEFINE_GAMEPLAY_TAG(Failure_Mutex,   "Skill.Failure.Mutex");
	UE_DEFINE_GAMEPLAY_TAG(Failure_Tier,    "Skill.Failure.Tier");
	UE_DEFINE_GAMEPLAY_TAG(Failure_Level,   "Skill.Failure.Level");
	UE_DEFINE_GAMEPLAY_TAG(Failure_Cost,    "Skill.Failure.Cost");
}

// ---- Construction / lifecycle ------------------------------------------------------------------

USkill_SkillComponent::USkill_SkillComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bWantsInitializeComponent = true;

	// Replicated state lives on this component (HARD RULE 5).
	SetIsReplicatedByDefault(true);

	// Wire the fast-array back-pointer so the per-entry client callbacks can reach us. Done in the ctor so
	// it is valid on both server and clients (replication reconstructs the array but not this Transient ptr,
	// which is why we set it from code rather than relying on serialization).
	Learned.OwnerComponent = this;
}

void USkill_SkillComponent::InitializeComponent()
{
	Super::InitializeComponent();

	// Re-assert the back-pointer in case the struct was default-constructed during component duplication.
	Learned.OwnerComponent = this;

	// On authority, sync the earned-point total from the resolved point source (or the fallback budget).
	if (HasAuthorityToMutate())
	{
		RefreshEarnedPointsFromSource();
	}
}

void USkill_SkillComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(USkill_SkillComponent, Learned);
	DOREPLIFETIME(USkill_SkillComponent, TotalEarnedPoints);
}

// ---- Authority gate ----------------------------------------------------------------------------

bool USkill_SkillComponent::HasAuthorityToMutate() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}

// ---- Mutators ----------------------------------------------------------------------------------

bool USkill_SkillComponent::LearnSkill(FGameplayTag SkillTag)
{
	// Guard authority at the TOP; clients route intent through a player-owned request component.
	if (!HasAuthorityToMutate())
	{
		UE_LOG(LogDP, Verbose, TEXT("[SkillTree] LearnSkill('%s') ignored on non-authority."), *SkillTag.ToString());
		return false;
	}
	return ApplyLearnAuthoritative(SkillTag) > 0;
}

bool USkill_SkillComponent::RankUp(FGameplayTag SkillTag)
{
	if (!HasAuthorityToMutate())
	{
		UE_LOG(LogDP, Verbose, TEXT("[SkillTree] RankUp('%s') ignored on non-authority."), *SkillTag.ToString());
		return false;
	}
	// RankUp is identical to LearnSkill once the prereqs are met; CanLearn already treats "already learned"
	// as a rank-up step (ResultingRank = current + 1). Sharing the path keeps gating in one place.
	return ApplyLearnAuthoritative(SkillTag) > 0;
}

int32 USkill_SkillComponent::ApplyLearnAuthoritative(const FGameplayTag& SkillTag)
{
	// Authoritative re-evaluation: never trust a client's claim that the skill is learnable.
	const FSkill_LearnEval Eval = CanLearn(SkillTag);
	if (!Eval.bAllowed)
	{
		UE_LOG(LogDP, Verbose, TEXT("[SkillTree] Learn('%s') denied (reason '%s')."),
			*SkillTag.ToString(), *Eval.FailureReason.ToString());
		return 0;
	}

	// Mutate the replicated array: add a new entry or bump the existing rank, then mark dirty for delta-rep.
	if (FSkill_LearnedEntry* Existing = Learned.FindBySkillTag(SkillTag))
	{
		Existing->Rank = Eval.ResultingRank;
		Learned.MarkItemDirty(*Existing);
	}
	else
	{
		FSkill_LearnedEntry& New = Learned.Entries.AddDefaulted_GetRef();
		New.SkillTag = SkillTag;
		New.Rank = Eval.ResultingRank;
		Learned.MarkItemDirty(New);
	}

	// Resolve + invoke the ability granter for this skill's linked ability. Absent granter (or unmapped
	// ability) => skip the grant; the learn still succeeds — a documented inert-default degrade.
	const FGameplayTag LinkedAbility = GetLinkedAbilityTag(SkillTag);
	if (LinkedAbility.IsValid())
	{
		TScriptInterface<ISkill_AbilityGranter> Granter = ResolveAbilityGranter();
		if (Granter.GetObject())
		{
			const bool bGranted = ISkill_AbilityGranter::Execute_GrantAbility(
				Granter.GetObject(), LinkedAbility, Eval.ResultingRank, SkillTag);
			UE_LOG(LogDP, Verbose, TEXT("[SkillTree] Skill '%s' linked ability '%s' grant (rank %d) -> %s."),
				*SkillTag.ToString(), *LinkedAbility.ToString(), Eval.ResultingRank,
				bGranted ? TEXT("ok") : TEXT("skipped"));
		}
		else
		{
			UE_LOG(LogDP, Verbose, TEXT("[SkillTree] No ISkill_AbilityGranter on owner; skipping ability '%s' for skill '%s'."),
				*LinkedAbility.ToString(), *SkillTag.ToString());
		}
	}

	// Broadcast locally on the server; OnRep paths broadcast on clients.
	OnSkillLearned.Broadcast(SkillTag, Eval.ResultingRank);
	OnPointsChanged.Broadcast(GetAvailablePoints());

	UE_LOG(LogDP, Log, TEXT("[SkillTree] Learned '%s' rank %d (available points now %d)."),
		*SkillTag.ToString(), Eval.ResultingRank, GetAvailablePoints());

	return Eval.ResultingRank;
}

bool USkill_SkillComponent::Respec()
{
	if (!HasAuthorityToMutate())
	{
		UE_LOG(LogDP, Verbose, TEXT("[SkillTree] Respec ignored on non-authority."));
		return false;
	}

	if (Learned.Entries.Num() == 0)
	{
		return false;
	}

	// Revoke each linked ability through the granter before clearing the entries.
	TScriptInterface<ISkill_AbilityGranter> Granter = ResolveAbilityGranter();
	if (Granter.GetObject())
	{
		for (const FSkill_LearnedEntry& Entry : Learned.Entries)
		{
			const FGameplayTag LinkedAbility = GetLinkedAbilityTag(Entry.SkillTag);
			if (LinkedAbility.IsValid())
			{
				ISkill_AbilityGranter::Execute_RevokeAbility(Granter.GetObject(), LinkedAbility, Entry.SkillTag);
			}
		}
	}

	// Clear all learned entries; spend resets to zero (TotalEarnedPoints is preserved).
	Learned.Entries.Reset();
	Learned.MarkArrayDirty();

	OnRespec.Broadcast();
	OnPointsChanged.Broadcast(GetAvailablePoints());

	UE_LOG(LogDP, Log, TEXT("[SkillTree] Respec complete; available points restored to %d."), GetAvailablePoints());
	return true;
}

// ---- Reads -------------------------------------------------------------------------------------

int32 USkill_SkillComponent::GetSkillRank(FGameplayTag SkillTag) const
{
	const FSkill_LearnedEntry* Entry = Learned.FindBySkillTag(SkillTag);
	return Entry ? Entry->Rank : 0;
}

int32 USkill_SkillComponent::GetPointsSpent() const
{
	int32 Spent = 0;
	for (const FSkill_LearnedEntry& Entry : Learned.Entries)
	{
		// Sum the cost of every learned rank of this skill (rank 1..Rank). The definition supplies per-rank cost.
		if (const USkill_SkillDefinition* Def = ResolveDefinition(Entry.SkillTag))
		{
			Spent += GetCumulativeCost(*Def, Entry.Rank);
		}
	}
	return Spent;
}

int32 USkill_SkillComponent::GetAvailablePoints() const
{
	return FMath::Max(0, TotalEarnedPoints - GetPointsSpent());
}

TArray<FSkill_LearnedRecord> USkill_SkillComponent::GetLearnedSkills() const
{
	TArray<FSkill_LearnedRecord> Records;
	Records.Reserve(Learned.Entries.Num());
	for (const FSkill_LearnedEntry& Entry : Learned.Entries)
	{
		if (Entry.IsValidEntry())
		{
			Records.Emplace(Entry.SkillTag, Entry.Rank);
		}
	}
	return Records;
}

FSkill_LearnEval USkill_SkillComponent::CanLearn(FGameplayTag SkillTag) const
{
	FSkill_LearnEval Eval;

	// Resolve the definition (designer data, locally available on all peers). Unknown skill => unlearnable.
	const USkill_SkillDefinition* Def = ResolveDefinition(SkillTag);
	if (!Def)
	{
		Eval.FailureReason = SkillFailureTags::Failure_Unknown;
		return Eval;
	}

	const int32 CurrentRank = GetSkillRank(SkillTag);
	const int32 TargetRank = CurrentRank + 1;
	Eval.ResultingRank = TargetRank;

	// Max-rank check: nothing left to learn (authored MaxRank, clamped to the settings safety cap).
	const int32 EffectiveMaxRank = GetEffectiveMaxRank(*Def);
	if (CurrentRank >= EffectiveMaxRank)
	{
		Eval.FailureReason = SkillFailureTags::Failure_MaxRank;
		return Eval;
	}

	// Per-step cost from the definition (each rank costs PointCost again).
	Eval.PointCost = FMath::Max(0, Def->PointCost);

	// Prerequisites: every active required skill must be at/above its required rank.
	TArray<FSkill_Prerequisite> ActivePrereqs;
	Def->GetActivePrerequisites(ActivePrereqs);
	for (const FSkill_Prerequisite& Prereq : ActivePrereqs)
	{
		if (GetSkillRank(Prereq.SkillTag) < FMath::Max(1, Prereq.MinRank))
		{
			Eval.FailureReason = SkillFailureTags::Failure_Prereq;
			return Eval;
		}
	}

	// Mutex group: only one skill in an exclusivity group may be learned. If learning a *new* skill (rank 0),
	// reject when any sibling in the same group is already learned. (Ranking up an already-learned skill in
	// the group is fine.)
	if (CurrentRank == 0 && Def->MutexGroup.IsValid())
	{
		for (const FSkill_LearnedEntry& Entry : Learned.Entries)
		{
			if (Entry.SkillTag == SkillTag)
			{
				continue;
			}
			if (const USkill_SkillDefinition* OtherDef = ResolveDefinition(Entry.SkillTag))
			{
				if (OtherDef->MutexGroup == Def->MutexGroup)
				{
					Eval.FailureReason = SkillFailureTags::Failure_Mutex;
					return Eval;
				}
			}
		}
	}

	// Tier-spend gate: the owner must have spent enough points in the tree to unlock this node's tier.
	// required-spend = node.Tier * RequiredSpentPerTier (0 disables the gate). Tunable, not a magic number.
	if (RequiredSpentPerTier > 0 && Def->Tier > 0)
	{
		const int32 RequiredSpend = Def->Tier * RequiredSpentPerTier;
		if (GetPointsSpent() < RequiredSpend)
		{
			Eval.FailureReason = SkillFailureTags::Failure_Tier;
			return Eval;
		}
	}

	// Level gate: required-level = node.Tier * RequiredLevelPerTier (0 disables). Owner level via the point
	// source; 0 (no source) means ungated by contract, so a positive requirement against level 0 fails closed.
	if (RequiredLevelPerTier > 0 && Def->Tier > 0)
	{
		const int32 RequiredLevel = Def->Tier * RequiredLevelPerTier;
		if (ResolveOwnerLevel() < RequiredLevel)
		{
			Eval.FailureReason = SkillFailureTags::Failure_Level;
			return Eval;
		}
	}

	// Affordability: available points must cover this step's cost.
	if (GetAvailablePoints() < Eval.PointCost)
	{
		Eval.FailureReason = SkillFailureTags::Failure_Cost;
		return Eval;
	}

	Eval.bAllowed = true;
	return Eval;
}

// ---- Save import -------------------------------------------------------------------------------

void USkill_SkillComponent::ImportFromSave(const TArray<FSkill_LearnedRecord>& Records, int32 InTotalEarnedPoints)
{
	if (!HasAuthorityToMutate())
	{
		UE_LOG(LogDP, Verbose, TEXT("[SkillTree] ImportFromSave ignored on non-authority (state arrives via replication)."));
		return;
	}

	// Revoke abilities for the current loadout before replacing it, so the granter's bookkeeping stays consistent.
	TScriptInterface<ISkill_AbilityGranter> Granter = ResolveAbilityGranter();
	if (Granter.GetObject())
	{
		for (const FSkill_LearnedEntry& Entry : Learned.Entries)
		{
			const FGameplayTag LinkedAbility = GetLinkedAbilityTag(Entry.SkillTag);
			if (LinkedAbility.IsValid())
			{
				ISkill_AbilityGranter::Execute_RevokeAbility(Granter.GetObject(), LinkedAbility, Entry.SkillTag);
			}
		}
	}

	// Replace the learned array from the records (skipping invalid ones).
	Learned.Entries.Reset(Records.Num());
	for (const FSkill_LearnedRecord& Record : Records)
	{
		if (Record.IsValidRecord())
		{
			FSkill_LearnedEntry& New = Learned.Entries.AddDefaulted_GetRef();
			New.SkillTag = Record.SkillTag;
			New.Rank = Record.Rank;
		}
	}
	Learned.MarkArrayDirty();

	// Restore the earned-point total. Persisted total wins over a freshly-resolved source so a loaded game is
	// deterministic; a point source can still grant more later. Clamped to the settings safety cap.
	int32 RestoredTotal = FMath::Max(0, InTotalEarnedPoints);
	if (const USkill_DeveloperSettings* Settings = GetDefault<USkill_DeveloperSettings>())
	{
		RestoredTotal = FMath::Min(RestoredTotal, FMath::Max(0, Settings->MaxTrackedPoints));
	}
	TotalEarnedPoints = RestoredTotal;

	// Re-grant the restored loadout's abilities so the live state matches the saved tree.
	if (Granter.GetObject())
	{
		for (const FSkill_LearnedEntry& Entry : Learned.Entries)
		{
			const FGameplayTag LinkedAbility = GetLinkedAbilityTag(Entry.SkillTag);
			if (LinkedAbility.IsValid())
			{
				ISkill_AbilityGranter::Execute_GrantAbility(Granter.GetObject(), LinkedAbility, Entry.Rank, Entry.SkillTag);
			}
		}
	}

	OnPointsChanged.Broadcast(GetAvailablePoints());
	UE_LOG(LogDP, Log, TEXT("[SkillTree] Imported %d learned skills, %d earned points from save."),
		Learned.Entries.Num(), TotalEarnedPoints);
}

// ---- Seam resolution ---------------------------------------------------------------------------

TScriptInterface<ISkill_AbilityGranter> USkill_SkillComponent::ResolveAbilityGranter() const
{
	TScriptInterface<ISkill_AbilityGranter> Result;

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return Result;
	}

	// Prefer the owning actor itself implementing the seam.
	if (Owner->Implements<USkill_AbilityGranter>())
	{
		Result.SetObject(Owner);
		Result.SetInterface(Cast<ISkill_AbilityGranter>(Owner));
		return Result;
	}

	// Otherwise scan components for the first that implements the seam (e.g. USkill_ActionGranterAdapter).
	if (UActorComponent* Found = Owner->FindComponentByInterface(USkill_AbilityGranter::StaticClass()))
	{
		Result.SetObject(Found);
		Result.SetInterface(Cast<ISkill_AbilityGranter>(Found));
	}
	return Result;
}

TScriptInterface<ISkill_PointSource> USkill_SkillComponent::ResolvePointSource() const
{
	TScriptInterface<ISkill_PointSource> Result;

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return Result;
	}

	if (Owner->Implements<USkill_PointSource>())
	{
		Result.SetObject(Owner);
		Result.SetInterface(Cast<ISkill_PointSource>(Owner));
		return Result;
	}

	if (UActorComponent* Found = Owner->FindComponentByInterface(USkill_PointSource::StaticClass()))
	{
		Result.SetObject(Found);
		Result.SetInterface(Cast<ISkill_PointSource>(Found));
	}
	return Result;
}

int32 USkill_SkillComponent::ResolveEarnedPointBudget() const
{
	TScriptInterface<ISkill_PointSource> Source = ResolvePointSource();
	if (Source.GetObject())
	{
		const int32 Earned = ISkill_PointSource::Execute_GetTotalEarnedPoints(Source.GetObject(), PointChannelTag);
		int32 Clamped = FMath::Max(0, Earned);
		// Defensive bound against a misbehaving source reporting an absurd total.
		if (const USkill_DeveloperSettings* Settings = GetDefault<USkill_DeveloperSettings>())
		{
			Clamped = FMath::Min(Clamped, FMath::Max(0, Settings->MaxTrackedPoints));
		}
		return Clamped;
	}
	// Documented inert default: no point source on the owner => use the editable fallback budget.
	return FMath::Max(0, FallbackPointBudget);
}

int32 USkill_SkillComponent::ResolveOwnerLevel() const
{
	TScriptInterface<ISkill_PointSource> Source = ResolvePointSource();
	if (Source.GetObject())
	{
		return FMath::Max(0, ISkill_PointSource::Execute_GetOwnerLevel(Source.GetObject()));
	}
	// No point source => ungated (contract: treat as level 0).
	return 0;
}

void USkill_SkillComponent::RefreshEarnedPointsFromSource()
{
	if (!HasAuthorityToMutate())
	{
		return;
	}
	const int32 NewBudget = ResolveEarnedPointBudget();
	if (NewBudget != TotalEarnedPoints)
	{
		TotalEarnedPoints = NewBudget;
		OnPointsChanged.Broadcast(GetAvailablePoints());
	}
}

// ---- Definition resolution ---------------------------------------------------------------------

USkill_SkillDefinition* USkill_SkillComponent::ResolveDefinition(const FGameplayTag& SkillTag) const
{
	if (!SkillTag.IsValid())
	{
		return nullptr;
	}

	// Prefer the explicitly-assigned tree (in-memory, no registry hit) when its package is already loaded.
	if (!SkillTree.IsNull())
	{
		if (USkill_SkillTreeDefinition* Tree = SkillTree.Get())
		{
			if (USkill_SkillDefinition* Node = Tree->FindNode(SkillTag))
			{
				return Node;
			}
		}
	}

	// Fall back to the core data registry, which resolves a DataTag to its asset (load-on-demand).
	if (UDP_DataRegistrySubsystem* Registry =
			FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_DataRegistrySubsystem>(this))
	{
		return Registry->Find<USkill_SkillDefinition>(SkillTag);
	}

	return nullptr;
}

int32 USkill_SkillComponent::GetEffectiveMaxRank(const USkill_SkillDefinition& Def) const
{
	int32 MaxRank = FMath::Max(1, Def.MaxRank);
	if (const USkill_DeveloperSettings* Settings = GetDefault<USkill_DeveloperSettings>())
	{
		MaxRank = FMath::Min(MaxRank, FMath::Max(1, Settings->AbsoluteMaxRank));
	}
	return MaxRank;
}

int32 USkill_SkillComponent::GetCumulativeCost(const USkill_SkillDefinition& Def, int32 RankToCount)
{
	// Every rank of a node costs PointCost again, so the cumulative spend is linear in the held rank.
	const int32 PerRank = FMath::Max(0, Def.PointCost);
	return PerRank * FMath::Max(0, RankToCount);
}

FGameplayTag USkill_SkillComponent::GetLinkedAbilityTag(const FGameplayTag& SkillTag) const
{
	if (const USkill_SkillDefinition* Def = ResolveDefinition(SkillTag))
	{
		return Def->GrantedAbilityTag;
	}
	return FGameplayTag();
}

// ---- Replication notify ------------------------------------------------------------------------

void USkill_SkillComponent::OnRep_TotalEarnedPoints()
{
	// The earned total changed on the server; clients re-broadcast their available-budget delegate for UI.
	OnPointsChanged.Broadcast(GetAvailablePoints());
}

void USkill_SkillComponent::NotifyLearnedEntryChanged(const FGameplayTag& SkillTag, int32 NewRank)
{
	// Driven by the fast-array client callbacks; mirror the server-side broadcasts locally.
	OnSkillLearned.Broadcast(SkillTag, NewRank);
	OnPointsChanged.Broadcast(GetAvailablePoints());
}

void USkill_SkillComponent::NotifyLearnedEntryRemoved(const FGameplayTag& SkillTag)
{
	OnPointsChanged.Broadcast(GetAvailablePoints());
}
