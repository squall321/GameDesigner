// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widget/InvUI_GridWidget.h"
#include "InputCoreTypes.h"
#include "InvUI_SpatialGridWidget.generated.h"

class UInvUI_SpatialIntentComponent;
class UInvUI_EncumbranceViewModel;
class UInvUI_SlotWidget;

/**
 * Grid view for SPATIAL (Tetris/Diablo) inventories.
 *
 * It ADDS ONLY new surface — it cannot touch the base's private slot pool nor C++-override the
 * BP-only OnSlotWidgetPlaced/OnGridRebuilt hooks. Specifically it adds:
 *  - NativeOnKeyDown (a real UUserWidget virtual) so a configured key rotates the hovered/focused
 *    item through the spatial intent component;
 *  - BindSpatial(...) to store the player-owned spatial intent component + the encumbrance VM;
 *  - OnSpanSizeForSlot, a NEW BlueprintImplementableEvent the spatial BP calls from the EXISTING
 *    OnSlotWidgetPlaced hook to size a multi-cell slot widget from the layout's ColumnSpan/RowSpan
 *    (read off the slot's FInvUI_SlotPosition via the grid VM). The cell pixel size is data-driven.
 *
 * Normal moves still flow through the base BindGrid + the move mediator; this widget only layers the
 * spatial-specific rotate/place affordances on top.
 */
UCLASS(Abstract, Blueprintable, meta = (DisplayName = "InvUI Spatial Grid Widget"))
class DESIGNPATTERNSINVENTORYUI_API UInvUI_SpatialGridWidget : public UInvUI_GridWidget
{
	GENERATED_BODY()

public:
	UInvUI_SpatialGridWidget();

	/**
	 * Bind the spatial helpers: the player-owned intent component (for rotate/place) and an optional
	 * encumbrance VM (for the weight/volume readout). Call after the base BindGrid.
	 */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Spatial")
	void BindSpatial(UInvUI_SpatialIntentComponent* InIntent, UInvUI_EncumbranceViewModel* InEncumbrance);

	/** The bound encumbrance VM, or null. */
	UFUNCTION(BlueprintPure, Category = "InvUI|Spatial")
	UInvUI_EncumbranceViewModel* GetEncumbranceViewModel() const { return EncumbranceViewModel; }

	/** The bound spatial intent component, or null. */
	UFUNCTION(BlueprintPure, Category = "InvUI|Spatial")
	UInvUI_SpatialIntentComponent* GetSpatialIntent() const { return SpatialIntent.Get(); }

	/**
	 * Issue a rotate of CurrentFocusSlot (or an explicit slot tag) through the intent component.
	 * Returns false if there is no spatial intent or no focused slot.
	 */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Spatial")
	bool RotateFocusedSlot();

	/** Set the slot the player is currently hovering/focusing, used as the rotate target. */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Spatial")
	void SetFocusedSlot(FGameplayTag SlotTag) { CurrentFocusSlot = SlotTag; }

	/** Look up the (ColumnSpan, RowSpan) the active layout assigned to SlotTag (1x1 if unknown). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "InvUI|Spatial")
	void GetSpanForSlot(FGameplayTag SlotTag, int32& OutColumnSpan, int32& OutRowSpan) const;

protected:
	//~ Begin UUserWidget
	virtual void NativeConstruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	//~ End UUserWidget

	/**
	 * Designer hook the spatial BP calls from the EXISTING OnSlotWidgetPlaced event to size a
	 * multi-cell slot widget from the layout span. The C++ owns the span lookup; the BP owns the
	 * actual canvas/grid-slot resize using CellPixelSize.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "InvUI|Spatial", meta = (DisplayName = "On Span Size For Slot"))
	void OnSpanSizeForSlot(UInvUI_SlotWidget* SlotWidget, int32 ColumnSpan, int32 RowSpan);

	/**
	 * Pixel size of one layout cell, used by the BP to convert a span into widget size. Defaults from
	 * UInvUI_Settings at construct time; never a hardcoded constant.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InvUI|Spatial", meta = (ClampMin = "1.0"))
	float CellPixelSize = 64.f;

	/**
	 * Key that rotates the focused item. Defaults to R; exposed so a game can rebind without code.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InvUI|Spatial")
	FKey RotateKey;

private:
	/** Player-owned spatial intent component used to route rotate/place (weak-held, non-owning). */
	UPROPERTY(Transient)
	TWeakObjectPtr<UInvUI_SpatialIntentComponent> SpatialIntent;

	/** Optional encumbrance VM for the weight/volume readout (owning ref kept alive while bound). */
	UPROPERTY(Transient)
	TObjectPtr<UInvUI_EncumbranceViewModel> EncumbranceViewModel = nullptr;

	/** Slot the player is currently hovering/focusing (the rotate target). */
	UPROPERTY(Transient)
	FGameplayTag CurrentFocusSlot;
};
