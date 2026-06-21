// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Item/RPG_ItemRoller.h"
#include "Item/RPG_RarityTable.h"
#include "Item/RPG_AffixDefinition.h"
#include "Item/RPG_ItemDefinition.h"
#include "Data/DPDataRegistrySubsystem.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"

FGameplayTag URPG_ItemRoller::ResolveItemTypeTag(const FGameplayTag& ItemTag) const
{
	if (!ItemTag.IsValid())
	{
		return FGameplayTag();
	}
	if (UDP_DataRegistrySubsystem* Registry =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_DataRegistrySubsystem>(this))
	{
		if (const URPG_ItemDefinition* Def = Registry->Find<URPG_ItemDefinition>(ItemTag))
		{
			return Def->ItemTypeTag;
		}
	}
	return FGameplayTag();
}

void URPG_ItemRoller::ResolveCandidateAffixes(FGameplayTag ItemTypeTag, FGameplayTag RarityTag,
	TArray<const URPG_AffixDefinition*>& Out) const
{
	Out.Reset();
	for (const TObjectPtr<URPG_AffixDefinition>& AffixPtr : AffixPool)
	{
		const URPG_AffixDefinition* Affix = AffixPtr.Get();
		if (!Affix)
		{
			continue;
		}
		if (!Affix->AllowsItemType(ItemTypeTag) || !Affix->AllowsRarity(RarityTag))
		{
			continue;
		}
		Out.Add(Affix);
	}
}

FRPG_ItemInstance URPG_ItemRoller::RollItem(FGameplayTag ItemTag, int32 ItemLevel, int32 Seed,
	FGameplayTag ForcedRarity) const
{
	FRPG_ItemInstance Instance;
	Instance.ItemTag = ItemTag;
	Instance.ItemLevel = FMath::Max(1, ItemLevel);
	Instance.UpgradeLevel = 0;
	Instance.CurrentDurability = BaseDurability;
	Instance.MaxDurability = BaseDurability;

	FRandomStream Stream(Seed);

	// 1) Rarity.
	FGameplayTag Rarity = ForcedRarity;
	if (!Rarity.IsValid() && RarityTable)
	{
		Rarity = RarityTable->RollRarity(Stream);
	}
	Instance.RarityTag = Rarity;

	// 2) Candidate affixes for this item type + rarity.
	const FGameplayTag ItemTypeTag = ResolveItemTypeTag(ItemTag);
	TArray<const URPG_AffixDefinition*> Candidates;
	ResolveCandidateAffixes(ItemTypeTag, Rarity, Candidates);

	// 3) Affix count band + budget from the rarity table (defensive zero envelope when no table).
	int32 MinAffixes = 0;
	int32 MaxAffixes = 0;
	int32 Budget = 0;
	int32 Sockets = 0;
	if (RarityTable)
	{
		RarityTable->GetAffixCountBand(Rarity, MinAffixes, MaxAffixes);
		Budget = RarityTable->GetAffixBudget(Rarity);
		Sockets = RarityTable->GetSocketCount(Rarity);
	}

	const int32 TargetAffixCount = (MaxAffixes > 0)
		? Stream.RandRange(FMath::Min(MinAffixes, MaxAffixes), MaxAffixes)
		: 0;

	// 4) Weighted, budget-bounded affix draw WITHOUT replacement.
	TArray<const URPG_AffixDefinition*> Remaining = Candidates;
	int32 RemainingBudget = (Budget > 0) ? Budget : MAX_int32; // no table budget -> count-bound only
	for (int32 Picked = 0; Picked < TargetAffixCount && Remaining.Num() > 0; ++Picked)
	{
		// Compute total weight of affordable candidates.
		double TotalWeight = 0.0;
		for (const URPG_AffixDefinition* Cand : Remaining)
		{
			if (Cand && Cand->BudgetCost <= RemainingBudget)
			{
				TotalWeight += FMath::Max(0.f, Cand->SelectionWeight);
			}
		}
		if (TotalWeight <= 0.0)
		{
			break; // nothing affordable left
		}

		const double Pick = static_cast<double>(Stream.FRand()) * TotalWeight;
		double Accum = 0.0;
		int32 ChosenIndex = INDEX_NONE;
		for (int32 Index = 0; Index < Remaining.Num(); ++Index)
		{
			const URPG_AffixDefinition* Cand = Remaining[Index];
			if (!Cand || Cand->BudgetCost > RemainingBudget)
			{
				continue;
			}
			Accum += FMath::Max(0.f, Cand->SelectionWeight);
			if (Pick <= Accum)
			{
				ChosenIndex = Index;
				break;
			}
		}
		if (ChosenIndex == INDEX_NONE)
		{
			break;
		}

		const URPG_AffixDefinition* Chosen = Remaining[ChosenIndex];
		Instance.Affixes.Add(Chosen->Roll(Instance.ItemLevel, Stream));
		RemainingBudget -= Chosen->BudgetCost;
		Remaining.RemoveAtSwap(ChosenIndex);
	}

	// 5) Sockets: fill with open sockets of the resolved type.
	const FGameplayTag SocketType = DefaultSocketType;
	for (int32 SocketIndex = 0; SocketIndex < Sockets; ++SocketIndex)
	{
		FRPG_ItemSocket Socket;
		Socket.SocketTypeTag = SocketType;
		Instance.Sockets.Add(Socket);
	}

	UE_LOG(LogDPData, Verbose,
		TEXT("[RPG_Roller] Rolled %s rarity=%s lvl=%d affixes=%d sockets=%d seed=%d"),
		*ItemTag.ToString(), *Rarity.ToString(), Instance.ItemLevel, Instance.Affixes.Num(),
		Instance.Sockets.Num(), Seed);

	return Instance;
}

FName URPG_ItemRoller::GetDataAssetType_Implementation() const
{
	return FName(TEXT("RPG_ItemRoller"));
}
