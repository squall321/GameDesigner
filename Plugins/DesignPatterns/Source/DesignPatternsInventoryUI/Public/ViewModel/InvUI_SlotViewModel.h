// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MVVM/DPViewModelBase.h"
#include "GameplayTagContainer.h"
#include "Engine/Texture2D.h"
#include "FieldNotification/FieldId.h"
#include "FieldNotification/IClassDescriptor.h"
#include "Seam/InvUI_ItemContainer.h"
#include "InvUI_SlotViewModel.generated.h"

/**
 * One slot's observable presentation state, bound by a single slot widget.
 *
 * The grid viewmodel owns a pool of these (one per laid-out slot) and pushes data into them as
 * the backend changes; the widget binds to the field-changed delegate (INotifyFieldValueChanged
 * on the base) and re-reads the getter for whichever field broadcast. The viewmodel holds NO
 * gameplay pointers: it is pure pushed display data (item tag, count, resolved name/icon, layout
 * cell, drop-target highlight). The icon is a soft pointer streamed by the owning grid viewmodel;
 * this slot vm just stores the resolved texture once available.
 *
 * Field notification: rather than rely on the UHT-generated FieldNotify descriptor (which needs
 * reflected UPROPERTYs tagged FieldNotify and a generated descriptor), this viewmodel publishes a
 * small hand-rolled IClassDescriptor that chains to the base's. Each observable field is a stable
 * EInvUI_SlotField id; setters call BroadcastField only when the value actually changes, so large
 * grids do minimal per-frame binding work. Widgets resolve a field by name (matching the enum
 * value names) via the base's K2_BroadcastFieldValueChanged / the descriptor.
 */
UCLASS(BlueprintType, meta = (DisplayName = "InvUI Slot ViewModel"))
class DESIGNPATTERNSINVENTORYUI_API UInvUI_SlotViewModel : public UDP_ViewModelBase
{
	GENERATED_BODY()

public:
	UInvUI_SlotViewModel();

	/** Stable, ordered ids for this viewmodel's observable fields. */
	enum class EField : int32
	{
		SlotTag = 0,
		ItemTag,
		Count,
		Empty,
		DisplayName,
		Description,
		Icon,
		QualityColor,
		Column,
		Row,
		DropTargetHighlight,
		Num
	};

	//~ Begin INotifyFieldValueChanged (override descriptor to expose our fields by name)
	virtual const UE::FieldNotification::IClassDescriptor& GetFieldNotificationDescriptor() const override;
	//~ End INotifyFieldValueChanged

	/**
	 * Push a new backend slot state into this viewmodel. Updates SlotTag/ItemTag/Count/empty
	 * fields, broadcasting only those that changed. Does NOT resolve the icon/name (the grid
	 * viewmodel drives async display resolution and calls ApplyDisplayInfo when it completes).
	 * Returns true if any observable field changed.
	 */
	bool SetSlotState(const FInvUI_SlotState& InState);

	/** Apply resolved display info (name/description/icon/quality). Broadcasts changed fields. */
	bool ApplyDisplayInfo(const FText& InName, const FText& InDescription, UTexture2D* InIcon, const FLinearColor& InQualityColor);

	/** Clear the resolved display info back to defaults (e.g. when a slot becomes empty). */
	void ClearDisplayInfo();

	/** Set this slot's cell position within the window layout (column/row, in cell units). */
	void SetCellPosition(int32 InColumn, int32 InRow);

	/** Toggle the "valid drop target" highlight while a drag hovers this slot. */
	void SetDropTargetHighlight(bool bInHighlighted);

	// --- Observable getters (widgets bind by field id / re-read these on broadcast) ---

	/** Identity of the slot this viewmodel represents. */
	UFUNCTION(BlueprintPure, Category = "InvUI|Slot") FGameplayTag GetSlotTag() const { return SlotTag; }
	/** Identity of the item in the slot (invalid when empty). */
	UFUNCTION(BlueprintPure, Category = "InvUI|Slot") FGameplayTag GetItemTag() const { return ItemTag; }
	/** Stack count (0 when empty). */
	UFUNCTION(BlueprintPure, Category = "InvUI|Slot") int32 GetCount() const { return Count; }
	/** True when the slot is empty. */
	UFUNCTION(BlueprintPure, Category = "InvUI|Slot") bool IsEmpty() const { return bEmpty; }
	/** Resolved display name. */
	UFUNCTION(BlueprintPure, Category = "InvUI|Slot") FText GetDisplayName() const { return DisplayName; }
	/** Resolved description / tooltip body. */
	UFUNCTION(BlueprintPure, Category = "InvUI|Slot") FText GetDescription() const { return Description; }
	/** Resolved icon texture (null until the async load completes). */
	UFUNCTION(BlueprintPure, Category = "InvUI|Slot") UTexture2D* GetIcon() const { return Icon; }
	/** Rarity/quality tint colour. */
	UFUNCTION(BlueprintPure, Category = "InvUI|Slot") FLinearColor GetQualityColor() const { return QualityColor; }
	/** Layout column (cell units). */
	UFUNCTION(BlueprintPure, Category = "InvUI|Slot") int32 GetColumn() const { return Column; }
	/** Layout row (cell units). */
	UFUNCTION(BlueprintPure, Category = "InvUI|Slot") int32 GetRow() const { return Row; }
	/** True while this slot is highlighted as a valid drop target. */
	UFUNCTION(BlueprintPure, Category = "InvUI|Slot") bool IsDropTargetHighlighted() const { return bDropTargetHighlight; }

	/** Resolve the FFieldId for one of this viewmodel's fields (for the grid vm / tests). */
	static UE::FieldNotification::FFieldId GetFieldId(EField Field);

private:
	/** Broadcast a field change by enum id (wraps the base BroadcastFieldValueChanged). */
	void BroadcastField(EField Field);

	/** Slot identity. */
	UPROPERTY(Transient)
	FGameplayTag SlotTag;

	/** Item identity (invalid when empty). */
	UPROPERTY(Transient)
	FGameplayTag ItemTag;

	/** Stack count. */
	UPROPERTY(Transient)
	int32 Count = 0;

	/** Cached emptiness flag (derived from ItemTag/Count) for cheap widget binding. */
	UPROPERTY(Transient)
	bool bEmpty = true;

	/** Resolved display name. */
	UPROPERTY(Transient)
	FText DisplayName;

	/** Resolved description. */
	UPROPERTY(Transient)
	FText Description;

	/** Resolved icon (strong while held so the widget can paint it). */
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> Icon = nullptr;

	/** Quality/rarity tint. */
	UPROPERTY(Transient)
	FLinearColor QualityColor = FLinearColor::White;

	/** Layout column (cell units). */
	UPROPERTY(Transient)
	int32 Column = 0;

	/** Layout row (cell units). */
	UPROPERTY(Transient)
	int32 Row = 0;

	/** Drop-target highlight flag. */
	UPROPERTY(Transient)
	bool bDropTargetHighlight = false;
};
