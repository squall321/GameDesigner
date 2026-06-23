// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Generation/Lvl_DungeonGraphRuleSet.h"
#include "Core/DPLog.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "Lvl_DungeonGraphRuleSet"

ULvl_DungeonGraphRuleSet::ULvl_DungeonGraphRuleSet()
{
	// Sensible, non-magic defaults are documented on each UPROPERTY; nothing computed here.
}

int32 ULvl_DungeonGraphRuleSet::GetEffectiveRoomCount(FRandomStream& Stream) const
{
	const int32 Min = FMath::Max(1, RoomCountRange.Min);
	const int32 Max = FMath::Max(Min, RoomCountRange.Max);
	return Stream.RandRange(Min, Max);
}

FIntPoint ULvl_DungeonGraphRuleSet::SampleRoomSize(FRandomStream& Stream) const
{
	const int32 WMin = FMath::Max(1, FMath::FloorToInt(FMath::Min(RoomWidthRangeCells.X, RoomWidthRangeCells.Y)));
	const int32 WMax = FMath::Max(WMin, FMath::FloorToInt(FMath::Max(RoomWidthRangeCells.X, RoomWidthRangeCells.Y)));
	const int32 HMin = FMath::Max(1, FMath::FloorToInt(FMath::Min(RoomHeightRangeCells.X, RoomHeightRangeCells.Y)));
	const int32 HMax = FMath::Max(HMin, FMath::FloorToInt(FMath::Max(RoomHeightRangeCells.X, RoomHeightRangeCells.Y)));
	return FIntPoint(Stream.RandRange(WMin, WMax), Stream.RandRange(HMin, HMax));
}

FVector ULvl_DungeonGraphRuleSet::CellToLocal(const FIntPoint& Cell) const
{
	// Centre of the cell, owner-local. The generator applies the owner transform on top.
	const float Size = FMath::Max(1.f, CellWorldSize);
	return FVector((Cell.X + 0.5f) * Size, (Cell.Y + 0.5f) * Size, 0.0);
}

const FLvl_WfcTileRule* ULvl_DungeonGraphRuleSet::PickTile(FRandomStream& Stream, ELvl_TileKind Kind,
	const FGameplayTagContainer& AllowedSet) const
{
	// Gather candidates that allow this kind and (when constrained) sit in the allowed-connector set.
	float TotalWeight = 0.f;
	TArray<const FLvl_WfcTileRule*, TInlineAllocator<16>> Candidates;
	for (const FLvl_WfcTileRule& Rule : TileRules)
	{
		if (!Rule.TileTag.IsValid() || !Rule.AllowsKind(Kind))
		{
			continue;
		}
		if (!AllowedSet.IsEmpty() && !AllowedSet.HasTag(Rule.TileTag))
		{
			continue;
		}
		Candidates.Add(&Rule);
		TotalWeight += FMath::Max(0.f, Rule.Weight);
	}

	if (Candidates.Num() == 0)
	{
		return nullptr;
	}

	// Uniform pick when all weights are zero; otherwise weighted.
	if (TotalWeight <= 0.f)
	{
		return Candidates[Stream.RandRange(0, Candidates.Num() - 1)];
	}

	float Roll = Stream.FRandRange(0.f, TotalWeight);
	for (const FLvl_WfcTileRule* Rule : Candidates)
	{
		Roll -= FMath::Max(0.f, Rule->Weight);
		if (Roll <= 0.f)
		{
			return Rule;
		}
	}
	return Candidates.Last();
}

#if WITH_EDITOR
EDataValidationResult ULvl_DungeonGraphRuleSet::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (GridDimensions.X < 1 || GridDimensions.Y < 1)
	{
		Context.AddError(LOCTEXT("BadGrid", "GridDimensions must be >= 1 in each axis."));
		Result = EDataValidationResult::Invalid;
	}

	if (RoomCountRange.Min < 1 || RoomCountRange.Max < RoomCountRange.Min)
	{
		Context.AddError(LOCTEXT("BadRoomCount", "RoomCountRange must have 1 <= Min <= Max."));
		Result = EDataValidationResult::Invalid;
	}

	// A room must be able to fit in the grid (defensive: warn, do not hard-fail, since the packer clamps).
	const int32 MaxRoomW = FMath::FloorToInt(FMath::Max(RoomWidthRangeCells.X, RoomWidthRangeCells.Y));
	const int32 MaxRoomH = FMath::FloorToInt(FMath::Max(RoomHeightRangeCells.X, RoomHeightRangeCells.Y));
	if (MaxRoomW > GridDimensions.X || MaxRoomH > GridDimensions.Y)
	{
		Context.AddWarning(LOCTEXT("RoomTooBig", "A room's max size exceeds the grid; the packer will clamp it."));
	}

	// Prefab stamps must name a class tag to be usable.
	for (const FLvl_PrefabStampRule& Stamp : PrefabStamps)
	{
		if (!Stamp.IsUsable())
		{
			Context.AddWarning(LOCTEXT("StampNoTag", "A prefab stamp has no ActorClassTag and will be skipped."));
		}
	}

	return Result;
}
#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE
