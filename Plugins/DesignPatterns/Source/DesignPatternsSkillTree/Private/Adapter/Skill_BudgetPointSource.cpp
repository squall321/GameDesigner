// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Adapter/Skill_BudgetPointSource.h"
#include "Core/DPLog.h"

#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

USkill_BudgetPointSource::USkill_BudgetPointSource()
{
	PrimaryComponentTick.bCanEverTick = false;
	bWantsInitializeComponent = true;

	// Per-channel budgets are replicated state on this UActorComponent (HARD RULE 5).
	SetIsReplicatedByDefault(true);
}

void USkill_BudgetPointSource::InitializeComponent()
{
	Super::InitializeComponent();

	// Seed any designer-authored starting budgets on the authority (e.g. a tutorial freebie). Clients receive
	// the seeded totals via replication.
	if (HasAuthorityToMutate())
	{
		for (const TPair<FGameplayTag, int32>& Pair : StartingBudgets)
		{
			if (Pair.Value > 0)
			{
				GrantPoints(Pair.Key, Pair.Value);
			}
		}
	}
}

void USkill_BudgetPointSource::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(USkill_BudgetPointSource, Budgets);
	DOREPLIFETIME(USkill_BudgetPointSource, OwnerLevel);
}

// ---- Authority ---------------------------------------------------------------------------------

bool USkill_BudgetPointSource::HasAuthorityToMutate() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}

// ---- Channel helpers ---------------------------------------------------------------------------

namespace
{
	/** Map an invalid (empty) channel argument onto the configured default channel. */
	FORCEINLINE FGameplayTag NormalizeChannel(const FGameplayTag& In, const FGameplayTag& Default)
	{
		return In.IsValid() ? In : Default;
	}
}

// ---- Mutators ----------------------------------------------------------------------------------

int32 USkill_BudgetPointSource::GrantPoints(FGameplayTag ChannelTag, int32 Amount)
{
	// Guard authority at the TOP; clients route grants through gameplay running on the server.
	if (!HasAuthorityToMutate())
	{
		UE_LOG(LogDP, Verbose, TEXT("[SkillTree] GrantPoints ignored on non-authority."));
		return GetChannelBudget(ChannelTag);
	}

	if (Amount <= 0)
	{
		// Negative/zero grants are rejected — revocation isn't supported here (use a respec on the skill
		// component instead). Return the unchanged total.
		UE_LOG(LogDP, Verbose, TEXT("[SkillTree] GrantPoints rejected non-positive amount %d."), Amount);
		return GetChannelBudget(ChannelTag);
	}

	const FGameplayTag Channel = NormalizeChannel(ChannelTag, DefaultChannelTag);

	if (FSkill_PointBudgetEntry* Existing = Budgets.FindByChannel(Channel))
	{
		Existing->TotalGranted += Amount;
		Budgets.MarkItemDirty(*Existing);
		UE_LOG(LogDP, Log, TEXT("[SkillTree] Granted %d points on channel '%s' (total %d)."),
			Amount, *Channel.ToString(), Existing->TotalGranted);
		return Existing->TotalGranted;
	}

	FSkill_PointBudgetEntry& New = Budgets.Entries.AddDefaulted_GetRef();
	New.ChannelTag = Channel;
	New.TotalGranted = Amount;
	Budgets.MarkItemDirty(New);

	UE_LOG(LogDP, Log, TEXT("[SkillTree] Opened channel '%s' with %d points."), *Channel.ToString(), Amount);
	return New.TotalGranted;
}

void USkill_BudgetPointSource::SetOwnerLevel(int32 InLevel)
{
	if (!HasAuthorityToMutate())
	{
		UE_LOG(LogDP, Verbose, TEXT("[SkillTree] SetOwnerLevel ignored on non-authority."));
		return;
	}
	OwnerLevel = FMath::Max(0, InLevel);
}

// ---- Reads -------------------------------------------------------------------------------------

int32 USkill_BudgetPointSource::GetChannelBudget(FGameplayTag ChannelTag) const
{
	const FGameplayTag Channel = NormalizeChannel(ChannelTag, DefaultChannelTag);
	return Budgets.GetTotalForChannel(Channel);
}

// ---- ISkill_PointSource ------------------------------------------------------------------------

int32 USkill_BudgetPointSource::GetTotalEarnedPoints_Implementation(FGameplayTag Channel) const
{
	// The "earned" half of the available-pool formula the skill component owns. An invalid Channel maps onto
	// the configured default; an unknown channel reads 0 (the seam's documented inert default).
	return GetChannelBudget(Channel);
}

int32 USkill_BudgetPointSource::GetOwnerLevel_Implementation() const
{
	return FMath::Max(0, OwnerLevel);
}
