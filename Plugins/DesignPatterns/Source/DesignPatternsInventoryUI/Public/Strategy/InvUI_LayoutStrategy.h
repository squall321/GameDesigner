// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "Seam/InvUI_ItemContainer.h"
#include "InvUI_LayoutStrategy.generated.h"

/**
 * The placement of one slot within the window's 2D layout grid, in CELL units (not pixels).
 * The view multiplies Column/Row by its own cell size to position the widget, so the layout
 * strategy stays resolution- and DPI-independent.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSINVENTORYUI_API FInvUI_SlotPosition
{
	GENERATED_BODY()

	/** Identity of the slot this position belongs to (echoed from its FInvUI_SlotState). */
	UPROPERTY(BlueprintReadOnly, Category = "InvUI|Layout")
	FGameplayTag SlotTag;

	/** Column index in cell units (0-based, left to right). */
	UPROPERTY(BlueprintReadOnly, Category = "InvUI|Layout")
	int32 Column = 0;

	/** Row index in cell units (0-based, top to bottom). */
	UPROPERTY(BlueprintReadOnly, Category = "InvUI|Layout")
	int32 Row = 0;

	/** Column span in cells (>=1). Lets a layout reserve wide slots (e.g. a 2x1 two-hander). */
	UPROPERTY(BlueprintReadOnly, Category = "InvUI|Layout", meta = (ClampMin = "1"))
	int32 ColumnSpan = 1;

	/** Row span in cells (>=1). */
	UPROPERTY(BlueprintReadOnly, Category = "InvUI|Layout", meta = (ClampMin = "1"))
	int32 RowSpan = 1;

	/** True when this entry maps to a real slot (false entries are not emitted). */
	UPROPERTY(BlueprintReadOnly, Category = "InvUI|Layout")
	bool bValid = false;

	FInvUI_SlotPosition() = default;
	FInvUI_SlotPosition(const FGameplayTag& InSlotTag, int32 InCol, int32 InRow)
		: SlotTag(InSlotTag), Column(InCol), Row(InRow), bValid(true) {}
};

/** Overall extent of a computed layout, in cell units, for the view to size its canvas. */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSINVENTORYUI_API FInvUI_LayoutResult
{
	GENERATED_BODY()

	/** One position per laid-out slot, in display order. */
	UPROPERTY(BlueprintReadOnly, Category = "InvUI|Layout")
	TArray<FInvUI_SlotPosition> Positions;

	/** Total columns the layout spans (>=0). */
	UPROPERTY(BlueprintReadOnly, Category = "InvUI|Layout")
	int32 ColumnCount = 0;

	/** Total rows the layout spans (>=0). */
	UPROPERTY(BlueprintReadOnly, Category = "InvUI|Layout")
	int32 RowCount = 0;
};

/**
 * Pluggable, data-driven strategy that maps an ordered list of slots to 2D cell positions.
 *
 * Three shipped flavours: grid (wrap into N columns), list (single column / single row), and
 * paper-doll (fixed named slots at designer-authored coordinates, e.g. equipment). A window
 * holds one as a UPROPERTY(Instanced); the grid viewmodel feeds it the (already sorted/filtered)
 * slots and republishes the resulting positions. NO slot/column counts are hardcoded — every
 * dimension is a UPROPERTY or, for paper-doll, a data-driven slot->coordinate table.
 */
UCLASS(Abstract, EditInlineNew, Blueprintable, BlueprintType, CollapseCategories,
	meta = (DisplayName = "InvUI Layout Strategy"))
class DESIGNPATTERNSINVENTORYUI_API UInvUI_LayoutStrategy : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Compute the cell positions for Slots (already in display order). Implementations must fill
	 * OutResult.Positions (one per laid-out slot) and the Column/Row counts. The base provides a
	 * default that defers to LayoutSlots.
	 */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Layout")
	void BuildLayout(const TArray<FInvUI_SlotState>& Slots, FInvUI_LayoutResult& OutResult) const;

	/** Strategy hook: do the actual placement. Override per flavour. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "InvUI|Layout")
	void LayoutSlots(const TArray<FInvUI_SlotState>& Slots, FInvUI_LayoutResult& OutResult) const;
	virtual void LayoutSlots_Implementation(const TArray<FInvUI_SlotState>& Slots, FInvUI_LayoutResult& OutResult) const;
};

/**
 * Grid layout: place slots left-to-right, wrapping to a new row every Columns slots. With
 * Columns <= 0 the grid degenerates to a single row (effectively unbounded width).
 */
UCLASS(EditInlineNew, meta = (DisplayName = "Grid Layout"))
class DESIGNPATTERNSINVENTORYUI_API UInvUI_GridLayout : public UInvUI_LayoutStrategy
{
	GENERATED_BODY()

public:
	/** Number of columns before wrapping. 0 or less = single unbounded row. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InvUI|Layout", meta = (ClampMin = "0"))
	int32 Columns = 8;

	virtual void LayoutSlots_Implementation(const TArray<FInvUI_SlotState>& Slots, FInvUI_LayoutResult& OutResult) const override;
};

/** List layout: one slot per row (vertical) or per column (horizontal). */
UCLASS(EditInlineNew, meta = (DisplayName = "List Layout"))
class DESIGNPATTERNSINVENTORYUI_API UInvUI_ListLayout : public UInvUI_LayoutStrategy
{
	GENERATED_BODY()

public:
	/** When true, lay out top-to-bottom (one column); otherwise left-to-right (one row). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InvUI|Layout")
	bool bVertical = true;

	virtual void LayoutSlots_Implementation(const TArray<FInvUI_SlotState>& Slots, FInvUI_LayoutResult& OutResult) const override;
};

/**
 * Paper-doll layout: place each slot at the designer-authored cell coordinate keyed by its
 * SlotTag (e.g. head at (1,0), main-hand at (0,2)). Slots with no mapping are dropped from the
 * layout (a paper-doll only shows its defined slots). Fully data-driven — no fixed slot set.
 */
UCLASS(EditInlineNew, meta = (DisplayName = "Paper-Doll Layout"))
class DESIGNPATTERNSINVENTORYUI_API UInvUI_PaperDollLayout : public UInvUI_LayoutStrategy
{
	GENERATED_BODY()

public:
	/** SlotTag -> cell coordinate. The placement position is exactly this entry. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InvUI|Layout")
	TMap<FGameplayTag, FIntPoint> SlotCoordinates;

	virtual void LayoutSlots_Implementation(const TArray<FInvUI_SlotState>& Slots, FInvUI_LayoutResult& OutResult) const override;
};
