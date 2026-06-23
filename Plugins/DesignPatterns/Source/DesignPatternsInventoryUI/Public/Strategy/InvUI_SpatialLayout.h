// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ScriptInterface.h"
#include "Strategy/InvUI_LayoutStrategy.h"
#include "Seam/InvUI_SpatialFootprintProvider.h"
#include "InvUI_SpatialLayout.generated.h"

/**
 * Tetris / Diablo-style SPATIAL bin-packing layout strategy.
 *
 * Each slot's footprint is read either from its FInvUI_SlotState.ItemPayload (when it carries an
 * FInvUI_SpatialFootprint) or, when the bound container implements IInvUI_SpatialFootprintProvider,
 * from Execute_GetSlotFootprint / Execute_GetSlotAnchorCell. A slot with an explicit anchor is
 * placed there; the rest are first-fit packed left-to-right, top-to-bottom over a transient
 * TBitArray occupancy grid (Columns wide, growing rows). The emitted FInvUI_SlotPosition sets
 * ColumnSpan/RowSpan from the (rotation-aware) footprint extent — those fields already ship on
 * FInvUI_SlotPosition, so multi-cell items need ZERO seam change.
 *
 * The occupancy grid is transient scratch built once per BuildLayout call; it is never the source
 * of truth (the backend owns the authoritative item positions). Columns/MaxRows have no hardcoded
 * gameplay meaning — Columns defaults from UInvUI_Settings when left at 0 and MaxRows is a safety
 * ceiling so a corrupt oversized footprint cannot grow the grid without bound.
 */
UCLASS(EditInlineNew, meta = (DisplayName = "Spatial (Grid) Layout"))
class DESIGNPATTERNSINVENTORYUI_API UInvUI_SpatialLayout : public UInvUI_LayoutStrategy
{
	GENERATED_BODY()

public:
	/**
	 * Number of columns the grid packs into. 0 = use UInvUI_Settings::DefaultSpatialColumns at
	 * layout time (so a designer can leave it data-driven). Always treated as >=1 internally.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InvUI|Spatial", meta = (ClampMin = "0"))
	int32 Columns = 0;

	/**
	 * Hard ceiling on rows the packer may grow to. 0 = use UInvUI_Settings::MaxSpatialRows. Prevents
	 * a pathological footprint from spinning the packer; overflow slots are emitted invalid (skipped).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InvUI|Spatial", meta = (ClampMin = "0"))
	int32 MaxRows = 0;

	//~ Begin UInvUI_LayoutStrategy
	virtual void LayoutSlots_Implementation(const TArray<FInvUI_SlotState>& Slots, FInvUI_LayoutResult& OutResult) const override;
	//~ End UInvUI_LayoutStrategy

	/**
	 * Resolve the effective footprint for a slot: the in-payload FInvUI_SpatialFootprint when present,
	 * else the container provider's footprint (via the optional ContainerForProvider seam), else 1x1.
	 * OutAnchor is set and bHasAnchor true when an explicit anchor cell is supplied.
	 */
	void ResolveFootprint(const FInvUI_SlotState& Slot, FInvUI_SpatialFootprint& OutFootprint,
		FIntPoint& OutAnchor, bool& bHasAnchor) const;

	/**
	 * First-fit search for a free Extent-sized rectangle in Occupancy (Cols wide, RowCount tall,
	 * grown as needed up to MaxRowsClamped). On success marks the cells and returns the top-left
	 * cell in OutCell; returns false if no fit exists within the row ceiling.
	 */
	static bool TryPlace(const FIntPoint& Extent, TBitArray<>& Occupancy, int32 Cols, int32& RowCount,
		int32 MaxRowsClamped, FIntPoint& OutCell);

	/**
	 * Mark the Extent-sized rectangle anchored at Cell as occupied, growing Occupancy/RowCount as
	 * needed. Returns false (without marking) if the rectangle would exceed the column or row bounds.
	 */
	static bool MarkAt(const FIntPoint& Cell, const FIntPoint& Extent, TBitArray<>& Occupancy,
		int32 Cols, int32& RowCount, int32 MaxRowsClamped);

	/**
	 * Optional spatial-footprint provider seam used when a slot has no in-payload footprint. The
	 * grid viewmodel/window sets this to the bound container before BuildLayout when the container
	 * implements IInvUI_SpatialFootprintProvider. Transient, non-owning (kept alive by its owner).
	 */
	UPROPERTY(Transient)
	TScriptInterface<IInvUI_SpatialFootprintProvider> ContainerForProvider;
};
