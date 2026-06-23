// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FieldNotification/FieldId.h"
#include "InvUI_ComparisonTooltipWidget.generated.h"

class UInvUI_ComparisonViewModel;

/**
 * View for the item-comparison tooltip (mirrors the slot widget contract: C++ owns the binding and
 * state transitions, the designer BP owns appearance). It is created by the slot's BP-only
 * MakeTooltipWidget hook; the WINDOW then calls BindComparison(VM) with a VM it already drove with
 * the hovered and equipped sides. The widget subscribes to the VM's field-changed notifications and
 * fires OnComparisonRefreshed so the BP repaints; BlueprintPure getters expose the rows.
 *
 * There is NO C++ override of MakeTooltipWidget (it is BP-only on the slot widget); this is a plain
 * UUserWidget bound externally, so it composes with the shipped slot/tooltip flow without changing it.
 */
UCLASS(Abstract, Blueprintable, meta = (DisplayName = "InvUI Comparison Tooltip Widget"))
class DESIGNPATTERNSINVENTORYUI_API UInvUI_ComparisonTooltipWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Bind the comparison VM and do an initial refresh. Unbinds any previous VM first. */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Comparison")
	void BindComparison(UInvUI_ComparisonViewModel* InViewModel);

	/** Detach from the current VM. Idempotent. */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Comparison")
	void UnbindComparison();

	/** The bound comparison VM, or null. */
	UFUNCTION(BlueprintPure, Category = "InvUI|Comparison")
	UInvUI_ComparisonViewModel* GetComparisonViewModel() const { return ViewModel; }

protected:
	//~ Begin UUserWidget
	virtual void NativeDestruct() override;
	//~ End UUserWidget

	/** Designer hook fired whenever the comparison VM broadcasts a change, so the BP repaints rows. */
	UFUNCTION(BlueprintImplementableEvent, Category = "InvUI|Comparison", meta = (DisplayName = "On Comparison Refreshed"))
	void OnComparisonRefreshed();

private:
	/** Routed from the VM's field-changed multicast; forwards to OnComparisonRefreshed. */
	void HandleViewModelFieldChanged(UObject* Object, UE::FieldNotification::FFieldId FieldId);

	/** Bind/unbind the VM's field-changed notifications. Idempotent. */
	void BindViewModelDelegates();
	void UnbindViewModelDelegates();

	/** The comparison VM this widget projects (owning ref keeps it alive while bound). */
	UPROPERTY(Transient)
	TObjectPtr<UInvUI_ComparisonViewModel> ViewModel = nullptr;

	/** True between bind and unbind, to guard double bind/unbind. */
	bool bBoundToViewModel = false;
};
