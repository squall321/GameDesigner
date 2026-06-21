// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Item/RPG_RarityTable.h"

const FRPG_RarityTier* URPG_RarityTable::FindTier(const FGameplayTag& RarityTag) const
{
	return Tiers.FindByPredicate([&RarityTag](const FRPG_RarityTier& Tier)
	{
		return Tier.RarityTag == RarityTag;
	});
}

int32 URPG_RarityTable::GetAffixBudget(FGameplayTag RarityTag) const
{
	const FRPG_RarityTier* Tier = FindTier(RarityTag);
	return Tier ? Tier->AffixBudget : 0;
}

int32 URPG_RarityTable::GetSocketCount(FGameplayTag RarityTag) const
{
	const FRPG_RarityTier* Tier = FindTier(RarityTag);
	return Tier ? Tier->SocketCount : 0;
}

void URPG_RarityTable::GetAffixCountBand(const FGameplayTag& RarityTag, int32& OutMin, int32& OutMax) const
{
	if (const FRPG_RarityTier* Tier = FindTier(RarityTag))
	{
		OutMin = Tier->MinAffixes;
		OutMax = FMath::Max(Tier->MinAffixes, Tier->MaxAffixes);
	}
	else
	{
		OutMin = 0;
		OutMax = 0;
	}
}

FGameplayTag URPG_RarityTable::RollRarity(FRandomStream& Stream) const
{
	// Sum positive weights, then draw a point and walk the cumulative distribution.
	double TotalWeight = 0.0;
	for (const FRPG_RarityTier& Tier : Tiers)
	{
		TotalWeight += FMath::Max(0.f, Tier.SelectionWeight);
	}
	if (TotalWeight <= 0.0)
	{
		// No weights authored: fall back to the first tier (or empty if none) so callers stay deterministic.
		return Tiers.Num() > 0 ? Tiers[0].RarityTag : FGameplayTag();
	}

	const double Pick = static_cast<double>(Stream.FRand()) * TotalWeight;
	double Accum = 0.0;
	for (const FRPG_RarityTier& Tier : Tiers)
	{
		Accum += FMath::Max(0.f, Tier.SelectionWeight);
		if (Pick <= Accum)
		{
			return Tier.RarityTag;
		}
	}
	// Floating-point edge: return the last weighted tier.
	return Tiers.Last().RarityTag;
}

FName URPG_RarityTable::GetDataAssetType_Implementation() const
{
	return FName(TEXT("RPG_RarityTable"));
}
