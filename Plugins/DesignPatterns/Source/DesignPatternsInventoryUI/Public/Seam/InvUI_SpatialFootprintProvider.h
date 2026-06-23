// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "InvUI_SpatialFootprintProvider.generated.h"

/**
 * Seam-neutral footprint of one item in a SPATIAL (Tetris/Diablo-style) container, in CELL units.
 *
 * This is a LOCAL/display value: a backend either carries one inside the existing
 * FInvUI_SlotState.ItemPayload (FInstancedStruct) for the spatial layout to read, or surfaces it
 * through IInvUI_SpatialFootprintProvider. Defaults are a 1x1, weightless cell so a non-spatial
 * backend's slots lay out exactly as before — nothing here is hardcoded gameplay data; Width/Height/
 * Weight/Volume are authored by the backend or designer.
 *
 * Every field is a UPROPERTY so UScriptStruct::CompareScriptStruct (used by
 * FInvUI_SlotState::EqualsForDisplay) dirty-diffs a rotation or size change and the grid viewmodel
 * re-broadcasts the affected slot.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSINVENTORYUI_API FInvUI_SpatialFootprint
{
	GENERATED_BODY()

	/** Unrotated width in cells (>=1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InvUI|Spatial", meta = (ClampMin = "1"))
	int32 Width = 1;

	/** Unrotated height in cells (>=1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InvUI|Spatial", meta = (ClampMin = "1"))
	int32 Height = 1;

	/** True when the item is currently rotated 90 degrees (swaps the effective extent). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InvUI|Spatial")
	bool bRotated = false;

	/** Per-item weight contribution toward an encumbrance total (units are the project's own). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InvUI|Spatial", meta = (ClampMin = "0.0"))
	float Weight = 0.f;

	/** Per-item volume contribution toward a volume total (units are the project's own). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InvUI|Spatial", meta = (ClampMin = "0.0"))
	float Volume = 0.f;

	FInvUI_SpatialFootprint() = default;
	FInvUI_SpatialFootprint(int32 InWidth, int32 InHeight)
		: Width(FMath::Max(1, InWidth)), Height(FMath::Max(1, InHeight)) {}

	/** Effective extent in cells, accounting for rotation (W/H swapped when bRotated). */
	FIntPoint GetExtent() const
	{
		const int32 W = FMath::Max(1, Width);
		const int32 H = FMath::Max(1, Height);
		return bRotated ? FIntPoint(H, W) : FIntPoint(W, H);
	}

	/** Number of cells the item occupies (independent of rotation). */
	int32 GetCellCount() const { return FMath::Max(1, Width) * FMath::Max(1, Height); }

	bool operator==(const FInvUI_SpatialFootprint& Other) const
	{
		return Width == Other.Width && Height == Other.Height && bRotated == Other.bRotated
			&& Weight == Other.Weight && Volume == Other.Volume;
	}
	bool operator!=(const FInvUI_SpatialFootprint& Other) const { return !(*this == Other); }
};

UINTERFACE(MinimalAPI, BlueprintType, meta = (DisplayName = "InvUI Spatial Footprint Provider"))
class UInvUI_SpatialFootprintProvider : public UInterface
{
	GENERATED_BODY()
};

/**
 * OPTIONAL read-only companion to IInvUI_ItemContainer for SPATIAL containers.
 *
 * Lives beside IInvUI_ItemContainer/IInvUI_ItemDisplay (module-local seam) so the spatial layout
 * can ask a backend for a slot's footprint and explicit anchor cell when the backend prefers a
 * callback over an in-payload FInvUI_SpatialFootprint. It is purely advisory and never mutates;
 * a backend that does not implement it is treated as 1x1, auto-packed. All methods are
 * BlueprintNativeEvent (callers use the Execute_ thunks).
 */
class IInvUI_SpatialFootprintProvider
{
	GENERATED_BODY()

public:
	/**
	 * Fill Out with the footprint of the item in SlotTag. Return false (leaving Out default 1x1)
	 * when the slot is empty/unknown or the backend has no footprint for it.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "InvUI|Spatial")
	bool GetSlotFootprint(FGameplayTag SlotTag, FInvUI_SpatialFootprint& Out) const;

	/**
	 * Fill OutCell with the explicit top-left anchor cell of the item in SlotTag (so a spatial
	 * backend that already stores positions keeps them). Return false to let the layout auto-pack
	 * the slot with first-fit placement.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "InvUI|Spatial")
	bool GetSlotAnchorCell(FGameplayTag SlotTag, FIntPoint& OutCell) const;
};
