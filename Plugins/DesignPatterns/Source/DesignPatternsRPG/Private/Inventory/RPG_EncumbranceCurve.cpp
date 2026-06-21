// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Inventory/RPG_EncumbranceCurve.h"
#include "Curves/CurveFloat.h"

float URPG_EncumbranceCurve::GetCapacityForStrength(float Strength) const
{
	float Capacity = BaseCapacity;
	if (CapacityPerStrength)
	{
		Capacity += CapacityPerStrength->GetFloatValue(Strength);
	}
	return FMath::Max(0.f, Capacity);
}

FGameplayTag URPG_EncumbranceCurve::ResolveTier(float LoadFraction, float& OutMoveSpeedMultiplier) const
{
	OutMoveSpeedMultiplier = 0.f;
	FGameplayTag Active;
	float BestThreshold = -1.f;

	for (const FRPG_EncumbranceTier& Tier : Tiers)
	{
		// Activate the highest-threshold tier whose threshold the current load meets.
		if (LoadFraction + KINDA_SMALL_NUMBER >= Tier.LoadFractionThreshold && Tier.LoadFractionThreshold > BestThreshold)
		{
			BestThreshold = Tier.LoadFractionThreshold;
			Active = Tier.TierTag;
			OutMoveSpeedMultiplier = Tier.MoveSpeedMultiplier;
		}
	}
	return Active;
}

FName URPG_EncumbranceCurve::GetDataAssetType_Implementation() const
{
	return FName(TEXT("RPG_EncumbranceCurve"));
}
