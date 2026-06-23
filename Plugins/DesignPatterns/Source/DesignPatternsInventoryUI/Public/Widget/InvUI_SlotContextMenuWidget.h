// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "Seam/InvUI_ItemContainer.h"
#include "InvUI_SlotContextMenuWidget.generated.h"

class UInvUI_SlotWidget;
class UInvUI_ContainerMediatorComponent;
class UInvUI_SpatialIntentComponent;

/**
 * Right-click slot context menu. It builds an action list filtered by the container's advertised
 * InvUITags::Cap_* (via the OPTIONAL IInvUI_ContainerCapabilities seam), and routes each chosen
 * action as the appropriate EXISTING intent:
 *  - Use / Equip / Drop / Split / quick-move -> the move mediator (RequestMoveByIdentity);
 *  - rotate -> the spatial intent component (RequestRotate).
 * Shift-click "quick move" reads the slot widget's PUBLIC GetContainerId/GetSlotTag and issues a
 * whole-stack move to a designer-supplied target container — adding NO override to the slot widget.
 *
 * C++ owns the contract; the BP owns the menu visuals (OnBuildActions lists the enabled actions).
 */
UCLASS(Abstract, Blueprintable, meta = (DisplayName = "InvUI Slot Context Menu Widget"))
class DESIGNPATTERNSINVENTORYUI_API UInvUI_SlotContextMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * Open the menu for SlotWidget. QuickMoveTarget is the container a Use/quick-move sends to (e.g.
	 * the other window's container); pass an invalid id to disable quick-move. Resolves the container
	 * capabilities and fires OnBuildActions with the enabled action set.
	 */
	UFUNCTION(BlueprintCallable, Category = "InvUI|ContextMenu")
	void OpenForSlot(UInvUI_SlotWidget* SlotWidget, FInvUI_ContainerInstanceId QuickMoveTarget);

	/** Supply the mediator + spatial intent the menu routes through (usually the window's). */
	UFUNCTION(BlueprintCallable, Category = "InvUI|ContextMenu")
	void SetRouters(UInvUI_ContainerMediatorComponent* InMediator, UInvUI_SpatialIntentComponent* InSpatialIntent);

	/**
	 * Invoke the action whose verb is IntentTag (one of InvUITags::Intent_*). Routes to the mediator
	 * or the spatial intent as appropriate. No-op for an unsupported / disabled action.
	 */
	UFUNCTION(BlueprintCallable, Category = "InvUI|ContextMenu")
	void InvokeAction(FGameplayTag IntentTag);

	/** Shift-click quick-move: move the whole stack to the QuickMoveTarget set in OpenForSlot. */
	UFUNCTION(BlueprintCallable, Category = "InvUI|ContextMenu")
	void QuickMove();

	/** The capabilities the bound container advertised at open time. */
	UFUNCTION(BlueprintPure, Category = "InvUI|ContextMenu")
	const FGameplayTagContainer& GetCapabilities() const { return Capabilities; }

protected:
	/**
	 * Designer hook fired after OpenForSlot resolves the container capabilities, so the BP builds the
	 * visible action buttons from the enabled set (Caps holds the advertised Cap_* tags).
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "InvUI|ContextMenu", meta = (DisplayName = "On Build Actions"))
	void OnBuildActions(const FGameplayTagContainer& Caps);

private:
	/** Resolve the live container for the bound slot, or empty. */
	TScriptInterface<IInvUI_ItemContainer> ResolveBoundContainer() const;

	/** The slot the menu is currently open for (weak; the grid owns it). */
	UPROPERTY(Transient)
	TWeakObjectPtr<UInvUI_SlotWidget> BoundSlot;

	/** Stable identity of the bound slot's container + slot, cached at open time. */
	UPROPERTY(Transient)
	FInvUI_ContainerInstanceId BoundContainerId;

	UPROPERTY(Transient)
	FGameplayTag BoundSlotTag;

	/** Destination for Use/quick-move (invalid disables quick-move). */
	UPROPERTY(Transient)
	FInvUI_ContainerInstanceId QuickMoveTargetId;

	/** Capabilities advertised by the bound container. */
	UPROPERTY(Transient)
	FGameplayTagContainer Capabilities;

	/** Move mediator the menu routes Use/Equip/Drop/Split/quick-move through (non-owning). */
	UPROPERTY(Transient)
	TWeakObjectPtr<UInvUI_ContainerMediatorComponent> Mediator;

	/** Spatial intent the menu routes rotate through (non-owning). */
	UPROPERTY(Transient)
	TWeakObjectPtr<UInvUI_SpatialIntentComponent> SpatialIntent;
};
