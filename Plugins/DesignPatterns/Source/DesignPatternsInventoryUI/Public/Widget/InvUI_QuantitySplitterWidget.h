// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FieldNotification/FieldId.h"
#include "Seam/InvUI_ItemContainer.h"
#include "InvUI_QuantitySplitterWidget.generated.h"

class UInvUI_QuantitySplitterViewModel;
class UInvUI_ContainerMediatorComponent;

/**
 * Skinnable quantity-splitter dialog. C++ owns the binding/state; the BP owns the slider/spinbox
 * visuals. The window opens it (BeginSplit) for a hovered stack, forwards slider changes through
 * SetSelected, and on confirm calls Confirm() which routes the partial move through the EXISTING
 * mediator on the VM (no new RPC). Subscribes to the VM's field changes to repaint via
 * OnSplitterRefreshed.
 */
UCLASS(Abstract, Blueprintable, meta = (DisplayName = "InvUI Quantity Splitter Widget"))
class DESIGNPATTERNSINVENTORYUI_API UInvUI_QuantitySplitterWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Bind the VM and (caller) Begin() the session. Unbinds any previous VM first. */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Splitter")
	void BindSplitter(UInvUI_QuantitySplitterViewModel* InViewModel);

	/** Detach from the current VM. Idempotent. */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Splitter")
	void UnbindSplitter();

	/** The bound splitter VM, or null. */
	UFUNCTION(BlueprintPure, Category = "InvUI|Splitter")
	UInvUI_QuantitySplitterViewModel* GetSplitterViewModel() const { return ViewModel; }

	/** Forward the slider/spinbox value to the VM (clamped there). */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Splitter")
	void SetSelected(int32 InSelected);

	/**
	 * Confirm the split: route Selected units through Mediator into ToContainer (empty ToSlot). The
	 * VM performs the validated move. The BP should then close/dismiss the dialog.
	 */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Splitter")
	void Confirm(UInvUI_ContainerMediatorComponent* Mediator, FInvUI_ContainerInstanceId ToContainer);

protected:
	//~ Begin UUserWidget
	virtual void NativeDestruct() override;
	//~ End UUserWidget

	/** Designer hook fired on every VM change so the BP repaints the current selection/range. */
	UFUNCTION(BlueprintImplementableEvent, Category = "InvUI|Splitter", meta = (DisplayName = "On Splitter Refreshed"))
	void OnSplitterRefreshed();

private:
	void HandleViewModelFieldChanged(UObject* Object, UE::FieldNotification::FFieldId FieldId);
	void BindViewModelDelegates();
	void UnbindViewModelDelegates();

	/** The splitter VM this widget projects (owning ref keeps it alive while bound). */
	UPROPERTY(Transient)
	TObjectPtr<UInvUI_QuantitySplitterViewModel> ViewModel = nullptr;

	bool bBoundToViewModel = false;
};
