// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Crowd/SimAg_FormationAsset.h"

USimAg_FormationAsset::USimAg_FormationAsset()
{
}

FVector USimAg_FormationAsset::GetSlotOffset(int32 SlotIndex, float Spacing) const
{
	SlotIndex = FMath::Max(0, SlotIndex);

	if (SlotOffsets.Num() > 0)
	{
		// Wrap into the authored pattern so a group larger than the pattern still gets distinct-ish slots.
		return SlotOffsets[SlotIndex % SlotOffsets.Num()];
	}

	// Procedural grid fallback: lay slots out in rows behind the anchor (-X is "behind").
	const int32 Columns = FMath::Max(1, GridColumns);
	const int32 Row = SlotIndex / Columns;
	const int32 Col = SlotIndex % Columns;
	// Centre each row on Y so the formation is symmetric around the anchor's facing.
	const float CentredCol = static_cast<float>(Col) - (static_cast<float>(Columns - 1) * 0.5f);
	const float SafeSpacing = FMath::Max(1.f, Spacing);
	return FVector(-static_cast<float>(Row) * SafeSpacing, CentredCol * SafeSpacing, 0.f);
}

FName USimAg_FormationAsset::GetDataAssetType_Implementation() const
{
	return FName("SimAg_Formation");
}
