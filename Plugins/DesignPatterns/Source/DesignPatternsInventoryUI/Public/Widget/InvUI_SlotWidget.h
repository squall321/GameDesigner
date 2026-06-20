// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "FieldNotification/FieldId.h"
#include "UObject/ScriptInterface.h"
#include "InputCoreTypes.h"

// Stable identity is owned by the InventoryUI seam layer; the per-slot ViewModel binding keeps
// this widget decoupled from any backend container.
#include "Seam/InvUI_ItemContainer.h"

#include "InvUI_SlotWidget.generated.h"

class UInvUI_SlotViewModel;
class UInvUI_ContainerMediatorComponent;
class UInvUI_DragDropOperation;
class UTexture2D;
struct FPointerEvent;
struct FGeometry;

/**
 * A single inventory slot widget — the leaf of the inventory UI.
 *
 * It is a pure projection of one UInvUI_SlotViewModel: it reads icon/count/empty/highlight
 * from the ViewModel and re-reads them when the ViewModel broadcasts a field change. It owns
 * NO backend pointer and NO index — its only addressable identity is (ContainerId, SlotTag),
 * which the model pushes onto it alongside the ViewModel.
 *
 * Interaction:
 *  - NativeOnDragDetected builds a UInvUI_DragDropOperation carrying IDENTITY ONLY.
 *  - NativeOnDrop reads the incoming operation and asks the per-player mediator to move BY
 *    IDENTITY (client requests; the server re-resolves and re-validates).
 *  - NativeOnDragEnter/Leave drive a highlight so the player sees a valid drop target.
 *  - Hover tooltip text/title is resolved through the IInvUI_ItemDisplay seam (model area)
 *    so this widget never depends on RPG/Survival item definitions.
 *
 * All concrete visuals are BlueprintImplementableEvent hooks: the C++ owns the contract and
 * state transitions, the designer owns appearance.
 */
UCLASS(Abstract, Blueprintable, meta = (DisplayName = "InvUI Slot Widget"))
class DESIGNPATTERNSINVENTORYUI_API UInvUI_SlotWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * Bind this slot to its ViewModel and addressable identity, supplying the mediator used
	 * to route moves. Unbinds any previous ViewModel first; safe to call repeatedly as the
	 * owning grid recycles slot widgets. Triggers an initial OnSlotRefreshed.
	 *
	 * @param InViewModel    The per-slot ViewModel to project (may be null to clear).
	 * @param InContainerId  Stable id of the container this slot belongs to.
	 * @param InSlotTag      Stable tag identifying this slot within its container.
	 * @param InMediator     Per-player mediator used to route identity moves (weak-held).
	 */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Slot")
	void BindSlot(UInvUI_SlotViewModel* InViewModel,
		FInvUI_ContainerInstanceId InContainerId,
		FGameplayTag InSlotTag,
		UInvUI_ContainerMediatorComponent* InMediator);

	/** Detach from the current ViewModel and clear identity. Idempotent. */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Slot")
	void UnbindSlot();

	/** The bound ViewModel, or null. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "InvUI|Slot")
	UInvUI_SlotViewModel* GetSlotViewModel() const { return SlotViewModel; }

	/** Stable id of the container this slot belongs to. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "InvUI|Slot")
	FInvUI_ContainerInstanceId GetContainerId() const { return ContainerId; }

	/** Stable tag identifying this slot within its container. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "InvUI|Slot")
	FGameplayTag GetSlotTag() const { return SlotTag; }

	/** True when the bound ViewModel currently has no item (empty cell). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "InvUI|Slot")
	bool IsEmpty() const;

protected:
	//~ Begin UUserWidget
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	virtual void NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent,
		UDragDropOperation*& OutOperation) override;

	virtual bool NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent,
		UDragDropOperation* InOperation) override;

	virtual void NativeOnDragEnter(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent,
		UDragDropOperation* InOperation) override;

	virtual void NativeOnDragLeave(const FDragDropEvent& InDragDropEvent,
		UDragDropOperation* InOperation) override;

	virtual void NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent) override;
	//~ End UUserWidget

	// --- Designer-facing visual hooks (C++ owns state, BP owns appearance) ---

	/** Re-read icon/count/empty from the bound ViewModel into the designer's widgets. */
	UFUNCTION(BlueprintImplementableEvent, Category = "InvUI|Slot", meta = (DisplayName = "On Slot Refreshed"))
	void OnSlotRefreshed();

	/** Drive the drop-target highlight on/off. */
	UFUNCTION(BlueprintImplementableEvent, Category = "InvUI|Slot", meta = (DisplayName = "On Set Highlighted"))
	void OnSetHighlighted(bool bHighlighted, bool bIsValidTarget);

	/** Drive the hover state visual on/off. */
	UFUNCTION(BlueprintImplementableEvent, Category = "InvUI|Slot", meta = (DisplayName = "On Set Hovered"))
	void OnSetHovered(bool bHovered);

	/**
	 * Build the drag-cursor widget shown under the pointer during a drag. Return null to
	 * use no visual. Called from NativeOnDragDetected after the operation is built.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "InvUI|Slot", meta = (DisplayName = "Make Drag Visual"))
	UUserWidget* MakeDragVisual(UInvUI_DragDropOperation* Operation);

	/**
	 * Build/return the tooltip widget for the hovered item. The default native path exposes the
	 * already-resolved title/description (the GridViewModel drives IInvUI_ItemDisplay resolution
	 * and pushes the results onto the slot ViewModel) via GetTooltipTitle/GetTooltipBody for the
	 * BP tooltip to read; override to supply a custom tooltip widget entirely.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "InvUI|Slot", meta = (DisplayName = "Make Tooltip Widget"))
	UWidget* MakeTooltipWidget();

	// --- Tooltip text/icon read from the bound ViewModel (resolved upstream via IInvUI_ItemDisplay) ---

	/** Localized display title for the hovered item (empty when the slot is empty/unresolved). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "InvUI|Slot")
	FText GetTooltipTitle() const;

	/** Localized body/description for the hovered item (empty when the slot is empty/unresolved). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "InvUI|Slot")
	FText GetTooltipBody() const;

	/** Icon texture for the hovered item (null until the upstream async load completes). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "InvUI|Slot")
	UTexture2D* GetSlotIcon() const;

	/** Rarity/quality tint colour the slot frame may use, read from the bound ViewModel. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "InvUI|Slot")
	FLinearColor GetSlotQualityColor() const;

	/**
	 * Minimum pointer travel (in slate units) before a press becomes a drag. Tunable so
	 * touch and mouse can differ; never a hardcoded constant in code.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InvUI|Slot",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float DragTriggerDistance = 8.0f;

	/**
	 * Mouse button that initiates a drag from this slot. Defaults to left button; exposed
	 * so a game can rebind without editing code.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InvUI|Slot")
	FKey DragTriggerKey;

private:
	/** Routed from the ViewModel's field-changed multicast; forwards to OnSlotRefreshed. */
	void HandleViewModelFieldChanged(UObject* Object, UE::FieldNotification::FFieldId FieldId);

	/** Bind to the ViewModel's field-changed notifications. Idempotent. */
	void BindViewModelDelegates();

	/** Remove field bindings from the current ViewModel. Idempotent. */
	void UnbindViewModelDelegates();

	/** The per-slot ViewModel this widget projects. Owning ref keeps it alive while bound. */
	UPROPERTY(Transient)
	TObjectPtr<UInvUI_SlotViewModel> SlotViewModel = nullptr;

	/** Non-owning ref to the per-player mediator used to route identity moves. */
	UPROPERTY(Transient)
	TWeakObjectPtr<UInvUI_ContainerMediatorComponent> Mediator;

	/** Stable id of the owning container. */
	UPROPERTY(Transient)
	FInvUI_ContainerInstanceId ContainerId;

	/** Stable tag identifying this slot. */
	UPROPERTY(Transient)
	FGameplayTag SlotTag;

	/** True between BindViewModelDelegates and UnbindViewModelDelegates. Guards double bind/unbind. */
	bool bBoundToViewModel = false;

	/** True while a drag is hovering this slot (highlight state). */
	bool bDropHighlighted = false;
};
