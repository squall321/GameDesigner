// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Strategy/InvUI_SpatialLayout.h"
#include "Settings/InvUI_Settings.h"
#include "Core/DPLog.h"

void UInvUI_SpatialLayout::ResolveFootprint(const FInvUI_SlotState& Slot,
	FInvUI_SpatialFootprint& OutFootprint, FIntPoint& OutAnchor, bool& bHasAnchor) const
{
	OutFootprint = FInvUI_SpatialFootprint();
	OutAnchor = FIntPoint::ZeroValue;
	bHasAnchor = false;

	// 1) Prefer an in-payload footprint (local/display value the backend embedded in ItemPayload).
	if (Slot.ItemPayload.GetScriptStruct() == FInvUI_SpatialFootprint::StaticStruct())
	{
		OutFootprint = Slot.ItemPayload.Get<FInvUI_SpatialFootprint>();
		return;
	}

	// 2) Else ask the optional container provider seam, when one was supplied.
	if (ContainerForProvider.GetObject() != nullptr)
	{
		FInvUI_SpatialFootprint Provided;
		if (IInvUI_SpatialFootprintProvider::Execute_GetSlotFootprint(
				ContainerForProvider.GetObject(), Slot.SlotTag, Provided))
		{
			OutFootprint = Provided;
		}

		FIntPoint Anchor;
		if (IInvUI_SpatialFootprintProvider::Execute_GetSlotAnchorCell(
				ContainerForProvider.GetObject(), Slot.SlotTag, Anchor))
		{
			OutAnchor = Anchor;
			bHasAnchor = true;
		}
	}
	// 3) Otherwise the default 1x1 footprint stands.
}

bool UInvUI_SpatialLayout::MarkAt(const FIntPoint& Cell, const FIntPoint& Extent,
	TBitArray<>& Occupancy, int32 Cols, int32& RowCount, int32 MaxRowsClamped)
{
	if (Cols <= 0 || Cell.X < 0 || Cell.Y < 0)
	{
		return false;
	}
	if (Cell.X + Extent.X > Cols)
	{
		return false; // would overflow the column bound
	}
	const int32 NeededRows = Cell.Y + Extent.Y;
	if (MaxRowsClamped > 0 && NeededRows > MaxRowsClamped)
	{
		return false; // would overflow the row ceiling
	}

	// Grow the occupancy bitset to cover the needed rows.
	if (NeededRows > RowCount)
	{
		Occupancy.Add(false, (NeededRows - RowCount) * Cols);
		RowCount = NeededRows;
	}

	for (int32 Y = Cell.Y; Y < Cell.Y + Extent.Y; ++Y)
	{
		for (int32 X = Cell.X; X < Cell.X + Extent.X; ++X)
		{
			Occupancy[Y * Cols + X] = true;
		}
	}
	return true;
}

bool UInvUI_SpatialLayout::TryPlace(const FIntPoint& Extent, TBitArray<>& Occupancy, int32 Cols,
	int32& RowCount, int32 MaxRowsClamped, FIntPoint& OutCell)
{
	if (Cols <= 0)
	{
		return false;
	}
	const int32 EW = FMath::Max(1, Extent.X);
	const int32 EH = FMath::Max(1, Extent.Y);
	if (EW > Cols)
	{
		return false; // item is wider than the whole grid; cannot place
	}

	// Scan rows top-to-bottom; grow by one row past the current extent each pass until placed or
	// the row ceiling is hit. Bounded by MaxRowsClamped (or, when 0, by a generous derived ceiling).
	const int32 RowCeiling = (MaxRowsClamped > 0) ? MaxRowsClamped : (RowCount + EH + 1);

	auto IsFree = [&Occupancy, Cols, RowCount](int32 X, int32 Y) -> bool
	{
		const int32 Index = Y * Cols + X;
		return (Index >= Occupancy.Num()) || !Occupancy[Index];
	};

	for (int32 Y = 0; Y + EH <= RowCeiling; ++Y)
	{
		for (int32 X = 0; X + EW <= Cols; ++X)
		{
			bool bFits = true;
			for (int32 dy = 0; dy < EH && bFits; ++dy)
			{
				for (int32 dx = 0; dx < EW && bFits; ++dx)
				{
					if (!IsFree(X + dx, Y + dy))
					{
						bFits = false;
					}
				}
			}
			if (bFits)
			{
				OutCell = FIntPoint(X, Y);
				return MarkAt(OutCell, FIntPoint(EW, EH), Occupancy, Cols, RowCount, MaxRowsClamped);
			}
		}
	}
	return false;
}

void UInvUI_SpatialLayout::LayoutSlots_Implementation(const TArray<FInvUI_SlotState>& Slots,
	FInvUI_LayoutResult& OutResult) const
{
	OutResult = FInvUI_LayoutResult();

	const UInvUI_Settings* Settings = UInvUI_Settings::Get();
	const int32 Cols = (Columns > 0)
		? Columns
		: (Settings ? Settings->GetEffectiveSpatialColumns() : 10);
	const int32 MaxRowsClamped = (MaxRows > 0)
		? MaxRows
		: (Settings ? FMath::Max(0, Settings->MaxSpatialRows) : 256);

	if (Cols <= 0)
	{
		return;
	}

	OutResult.Positions.Reserve(Slots.Num());

	// Transient occupancy scratch (row-major, Cols wide). Grown lazily by MarkAt/TryPlace.
	TBitArray<> Occupancy;
	int32 RowCount = 0;

	// Pass 1: honour explicit anchors first so first-fit packing flows around fixed items.
	struct FPending { int32 SlotIndex; FInvUI_SpatialFootprint Footprint; };
	TArray<FPending> AutoPack;
	AutoPack.Reserve(Slots.Num());

	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		const FInvUI_SlotState& Slot = Slots[i];

		FInvUI_SpatialFootprint Footprint;
		FIntPoint Anchor;
		bool bHasAnchor = false;
		ResolveFootprint(Slot, Footprint, Anchor, bHasAnchor);

		if (bHasAnchor)
		{
			const FIntPoint Extent = Footprint.GetExtent();
			if (MarkAt(Anchor, Extent, Occupancy, Cols, RowCount, MaxRowsClamped))
			{
				FInvUI_SlotPosition Pos(Slot.SlotTag, Anchor.X, Anchor.Y);
				Pos.ColumnSpan = FMath::Max(1, Extent.X);
				Pos.RowSpan = FMath::Max(1, Extent.Y);
				OutResult.Positions.Add(Pos);
				continue;
			}
			// Anchor overlapped / out of bounds: fall through to auto-packing.
			UE_LOG(LogDP, Verbose,
				TEXT("UInvUI_SpatialLayout: anchor (%d,%d) for slot %s did not fit; auto-packing."),
				Anchor.X, Anchor.Y, *Slot.SlotTag.ToString());
		}

		AutoPack.Add({ i, Footprint });
	}

	// Pass 2: first-fit pack the rest.
	for (const FPending& P : AutoPack)
	{
		const FInvUI_SlotState& Slot = Slots[P.SlotIndex];
		const FIntPoint Extent = P.Footprint.GetExtent();

		FIntPoint Cell;
		if (TryPlace(Extent, Occupancy, Cols, RowCount, MaxRowsClamped, Cell))
		{
			FInvUI_SlotPosition Pos(Slot.SlotTag, Cell.X, Cell.Y);
			Pos.ColumnSpan = FMath::Max(1, Extent.X);
			Pos.RowSpan = FMath::Max(1, Extent.Y);
			OutResult.Positions.Add(Pos);
		}
		else
		{
			// No room within the ceiling: emit an invalid position so the view skips it (the
			// FInvUI_SlotPosition default ctor leaves bValid=false).
			UE_LOG(LogDP, Warning,
				TEXT("UInvUI_SpatialLayout: slot %s (%dx%d) did not fit in %d columns x %d row ceiling."),
				*Slot.SlotTag.ToString(), Extent.X, Extent.Y, Cols, MaxRowsClamped);
			OutResult.Positions.Add(FInvUI_SlotPosition());
		}
	}

	OutResult.ColumnCount = Cols;
	OutResult.RowCount = RowCount;
}
