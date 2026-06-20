// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "FieldNotification/FieldId.h"

#include "Seam/InvUI_ItemContainer.h"

#include "InvUI_GridWidget.generated.h"

class UInvUI_SlotWidget;
class UInvUI_SlotViewModel;
class UInvUI_GridViewModel;
class UInvUI_ContainerMediatorComponent;
class UPanelWidget;

/**
 * A panel of slot widgets projecting one UInvUI_GridViewModel.
 *
 * The model layer's grid ViewModel does the heavy lifting (binds the container, applies the sort
 * and layout strategies, maintains a pooled set of per-slot ViewModels, resolves icons async, and
 * bumps a StructureRevision when the slot SET/order changes). THIS widget is the thin view: it
 * binds to that ViewModel, listens for the coarse structure change, and realises/recycles a
 * UInvUI_SlotWidget per displayed slot ViewModel — each slot widget then observes its own slot VM
 * for fine-grained icon/count updates. By swapping the ViewModel's layout strategy the same widget
 * renders bags, hotbars, equipment dolls, crafting matrices and shops.
 *
 * Split/merge affordances are recognised here and routed to the per-player mediator as identity
 * moves — the grid never mutates the backend itself.
 */
UCLASS(Abstract, Blueprintable, meta = (DisplayName = "InvUI Grid Widget"))
class DESIGNPATTERNSINVENTORYUI_API UInvUI_GridWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * Bind this grid to its ViewModel and the routing mediator, then build the slot widgets.
	 * Unbinds any previous ViewModel first; safe to call repeatedly.
	 *
	 * @param InViewModel  The grid ViewModel describing the displayed slot VMs + layout.
	 * @param InMediator   Per-player mediator used to route identity moves (weak-held).
	 */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Grid")
	void BindGrid(UInvUI_GridViewModel* InViewModel, UInvUI_ContainerMediatorComponent* InMediator);

	/** Detach from the current ViewModel and clear all child slot widgets. Idempotent. */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Grid")
	void UnbindGrid();

	/** The bound grid ViewModel, or null. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "InvUI|Grid")
	UInvUI_GridViewModel* GetGridViewModel() const { return GridViewModel; }

	/** Stable id of the container this grid currently shows. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "InvUI|Grid")
	FInvUI_ContainerInstanceId GetContainerId() const;

	/**
	 * Realise/recycle one slot widget per displayed slot ViewModel and (re)bind each. Cheap to call
	 * on every coarse structure change reported by the ViewModel.
	 */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Grid")
	void RebuildSlots();

	/**
	 * Request a stack SPLIT: move SplitCount units out of the slot, destination chosen by the
	 * server (empty target slot tag). Routed to the mediator as an identity move. No-op if invalid.
	 */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Grid")
	void RequestSplitStack(FGameplayTag SlotTag, int32 SplitCount);

	/**
	 * Request a stack MERGE: move the entire FromSlot stack onto ToSlot within this container (the
	 * backend coalesces when item tags match). Routed to the mediator.
	 */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Grid")
	void RequestMergeStacks(FGameplayTag FromSlotTag, FGameplayTag ToSlotTag);

protected:
	//~ Begin UUserWidget
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	//~ End UUserWidget

	/**
	 * Resolve the panel that hosts the child slot widgets. The default returns the SlotContainer
	 * bound widget; override in BP to host slots elsewhere.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "InvUI|Grid", meta = (DisplayName = "Resolve Slot Container"))
	UPanelWidget* ResolveSlotContainer();
	virtual UPanelWidget* ResolveSlotContainer_Implementation();

	/**
	 * Designer hook fired after a child slot widget is created and added, with its layout cell, so
	 * the panel (canvas slot, grid cell, doll anchor) can be positioned from the slot VM's column/row.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "InvUI|Grid", meta = (DisplayName = "On Slot Widget Placed"))
	void OnSlotWidgetPlaced(UInvUI_SlotWidget* SlotWidget, int32 Column, int32 Row);

	/** Designer hook fired after a full rebuild completes, with the layout extent (cells). */
	UFUNCTION(BlueprintImplementableEvent, Category = "InvUI|Grid", meta = (DisplayName = "On Grid Rebuilt"))
	void OnGridRebuilt(int32 ColumnCount, int32 RowCount);

	/**
	 * Widget class instantiated for each slot. Must derive from UInvUI_SlotWidget. Data-driven so
	 * the same grid renders different slot looks per screen; never hardcoded.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InvUI|Grid")
	TSubclassOf<UInvUI_SlotWidget> SlotWidgetClass;

	/**
	 * Optional bound panel the slots are added to. If unset, ResolveSlotContainer must be overridden
	 * in BP. BindWidgetOptional so designers can name a panel "SlotContainer".
	 */
	UPROPERTY(BlueprintReadOnly, Category = "InvUI|Grid", meta = (BindWidgetOptional))
	TObjectPtr<UPanelWidget> SlotContainer = nullptr;

private:
	/** Routed from the grid ViewModel's field-changed multicast; rebuilds slot widgets on structure changes. */
	void HandleViewModelFieldChanged(UObject* Object, UE::FieldNotification::FFieldId FieldId);

	/** Bind to the grid ViewModel's field-changed notifications. Idempotent. */
	void BindViewModelDelegates();

	/** Remove field bindings from the current grid ViewModel. Idempotent. */
	void UnbindViewModelDelegates();

	/** Tear down all realised child slot widgets and clear the pool. */
	void ClearSlotWidgets();

	/** The grid ViewModel this widget projects. Owning ref keeps it alive while bound. */
	UPROPERTY(Transient)
	TObjectPtr<UInvUI_GridViewModel> GridViewModel = nullptr;

	/** Non-owning ref to the per-player mediator used to route identity moves. */
	UPROPERTY(Transient)
	TWeakObjectPtr<UInvUI_ContainerMediatorComponent> Mediator;

	/** Pool of realised slot widgets, index-aligned with the ViewModel's displayed slot order. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInvUI_SlotWidget>> SlotWidgets;

	/** True between BindViewModelDelegates and UnbindViewModelDelegates. */
	bool bBoundToViewModel = false;

	/** Last StructureRevision a rebuild was performed for, to skip redundant rebuilds. */
	int32 LastStructureRevision = -1;
};
