// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Strategy/InvUI_LayoutStrategy.h"
#include "Core/DPLog.h"

//=============================== UInvUI_LayoutStrategy ===============================

void UInvUI_LayoutStrategy::BuildLayout(const TArray<FInvUI_SlotState>& Slots, FInvUI_LayoutResult& OutResult) const
{
	OutResult = FInvUI_LayoutResult();
	LayoutSlots(Slots, OutResult);
}

void UInvUI_LayoutStrategy::LayoutSlots_Implementation(const TArray<FInvUI_SlotState>& Slots, FInvUI_LayoutResult& OutResult) const
{
	// Default: single row, one cell per slot.
	OutResult.Positions.Reset(Slots.Num());
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		OutResult.Positions.Emplace(Slots[i].SlotTag, /*Col=*/i, /*Row=*/0);
	}
	OutResult.ColumnCount = Slots.Num();
	OutResult.RowCount = Slots.Num() > 0 ? 1 : 0;
}

//================================= UInvUI_GridLayout =================================

void UInvUI_GridLayout::LayoutSlots_Implementation(const TArray<FInvUI_SlotState>& Slots, FInvUI_LayoutResult& OutResult) const
{
	OutResult.Positions.Reset(Slots.Num());

	const int32 EffectiveColumns = (Columns > 0) ? Columns : FMath::Max(1, Slots.Num());
	int32 MaxColumnUsed = 0;
	int32 MaxRowUsed = 0;

	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		const int32 Col = i % EffectiveColumns;
		const int32 Row = i / EffectiveColumns;
		OutResult.Positions.Emplace(Slots[i].SlotTag, Col, Row);
		MaxColumnUsed = FMath::Max(MaxColumnUsed, Col);
		MaxRowUsed = FMath::Max(MaxRowUsed, Row);
	}

	OutResult.ColumnCount = Slots.Num() > 0 ? (MaxColumnUsed + 1) : 0;
	OutResult.RowCount = Slots.Num() > 0 ? (MaxRowUsed + 1) : 0;
}

//================================= UInvUI_ListLayout =================================

void UInvUI_ListLayout::LayoutSlots_Implementation(const TArray<FInvUI_SlotState>& Slots, FInvUI_LayoutResult& OutResult) const
{
	OutResult.Positions.Reset(Slots.Num());

	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		const int32 Col = bVertical ? 0 : i;
		const int32 Row = bVertical ? i : 0;
		OutResult.Positions.Emplace(Slots[i].SlotTag, Col, Row);
	}

	if (Slots.Num() == 0)
	{
		OutResult.ColumnCount = 0;
		OutResult.RowCount = 0;
	}
	else if (bVertical)
	{
		OutResult.ColumnCount = 1;
		OutResult.RowCount = Slots.Num();
	}
	else
	{
		OutResult.ColumnCount = Slots.Num();
		OutResult.RowCount = 1;
	}
}

//=============================== UInvUI_PaperDollLayout ==============================

void UInvUI_PaperDollLayout::LayoutSlots_Implementation(const TArray<FInvUI_SlotState>& Slots, FInvUI_LayoutResult& OutResult) const
{
	OutResult.Positions.Reset();

	int32 MaxColumn = -1;
	int32 MaxRow = -1;

	for (const FInvUI_SlotState& Slot : Slots)
	{
		const FIntPoint* Coord = SlotCoordinates.Find(Slot.SlotTag);
		if (Coord == nullptr)
		{
			// A paper-doll only displays its mapped slots; skip the rest.
			continue;
		}
		OutResult.Positions.Emplace(Slot.SlotTag, Coord->X, Coord->Y);
		MaxColumn = FMath::Max(MaxColumn, Coord->X);
		MaxRow = FMath::Max(MaxRow, Coord->Y);
	}

	OutResult.ColumnCount = (MaxColumn >= 0) ? (MaxColumn + 1) : 0;
	OutResult.RowCount = (MaxRow >= 0) ? (MaxRow + 1) : 0;
}
