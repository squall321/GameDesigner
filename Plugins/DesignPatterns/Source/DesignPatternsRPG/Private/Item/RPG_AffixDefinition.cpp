// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Item/RPG_AffixDefinition.h"
#include "Curves/CurveFloat.h"

URPG_AffixDefinition::URPG_AffixDefinition()
{
	// Default to a degenerate [0,0] range so an unconfigured affix rolls a harmless zero rather than NaN.
	MagnitudeRange = FFloatRange(FFloatRangeBound::Inclusive(0.f), FFloatRangeBound::Inclusive(0.f));
}

bool URPG_AffixDefinition::AllowsItemType(FGameplayTag ItemTypeTag) const
{
	if (AllowedItemTypes.IsEmpty())
	{
		return true;
	}
	return ItemTypeTag.IsValid() && AllowedItemTypes.HasTag(ItemTypeTag);
}

bool URPG_AffixDefinition::AllowsRarity(FGameplayTag RarityTag) const
{
	if (RarityRequirement.IsEmpty())
	{
		return true;
	}
	FGameplayTagContainer Container;
	if (RarityTag.IsValid())
	{
		Container.AddTag(RarityTag);
	}
	return RarityRequirement.Matches(Container);
}

FRPG_ItemAffix URPG_AffixDefinition::Roll(int32 ItemLevel, FRandomStream& Stream) const
{
	FRPG_ItemAffix Affix;
	Affix.AffixDefTag = DataTag;
	Affix.AttributeTag = AttributeTag;
	Affix.Op = Op;

	// Resolve the inclusive [Min,Max] bounds defensively (an open/unbounded range collapses to its set bound).
	const float MinValue = MagnitudeRange.HasLowerBound() ? MagnitudeRange.GetLowerBoundValue() : 0.f;
	const float MaxValue = MagnitudeRange.HasUpperBound() ? MagnitudeRange.GetUpperBoundValue() : MinValue;
	const float Lo = FMath::Min(MinValue, MaxValue);
	const float Hi = FMath::Max(MinValue, MaxValue);

	float Rolled = Stream.FRandRange(Lo, Hi);

	// Scale by item level if a curve is authored (X = item level, Y = multiplier).
	if (MagnitudeByItemLevel)
	{
		const float Scale = MagnitudeByItemLevel->GetFloatValue(static_cast<float>(FMath::Max(1, ItemLevel)));
		Rolled *= Scale;
	}

	Affix.RolledMagnitude = FSeam_NetValue::MakeFloat(static_cast<double>(Rolled));
	return Affix;
}

FName URPG_AffixDefinition::GetDataAssetType_Implementation() const
{
	return FName(TEXT("RPG_Affix"));
}
