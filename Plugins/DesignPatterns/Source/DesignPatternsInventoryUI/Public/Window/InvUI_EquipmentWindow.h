// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Window/InvUI_WindowBase.h"
#include "GameplayTagContainer.h"
#include "Strategy/InvUI_LayoutStrategy.h"
#include "InvUI_EquipmentWindow.generated.h"

/**
 * Paper-doll EQUIPMENT screen.
 *
 * Reuses the SHIPPED UInvUI_PaperDollLayout (its SlotCoordinates table places named equipment slots
 * at designer cells) and SetShowEmptySlots(true) so empty slots render. The equipment container is
 * reached ONLY as an IInvUI_ItemContainer id resolved from the registry — its concrete backend is
 * never hard-included. ResolveRoleForContainer is overridden so a flat BindContainers list maps the
 * equipment id to this window's single equipment role.
 *
 * Hovering an equipment slot can drive a comparison tooltip: the window exposes the equipped
 * container as the comparison baseline so a bag-side hover diffs against the worn item.
 */
UCLASS(Abstract, Blueprintable, meta = (DisplayName = "InvUI Equipment Window"))
class DESIGNPATTERNSINVENTORYUI_API UInvUI_EquipmentWindow : public UInvUI_WindowBase
{
	GENERATED_BODY()

public:
	UInvUI_EquipmentWindow();

	/**
	 * The paper-doll layout this window installs on its equipment grid VM. Designer authors the
	 * SlotCoordinates (slot tag -> cell). Instanced so it is editable per window asset.
	 */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadWrite, Category = "InvUI|Equipment")
	TObjectPtr<UInvUI_PaperDollLayout> DollLayout;

	/** Role tag of this window's single equipment grid (the grid registered under it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InvUI|Equipment")
	FGameplayTag EquipmentRole;

	/**
	 * Designer convenience: merge an authored slot tag -> cell map into the doll layout's
	 * SlotCoordinates before binding. Lets a window asset declare its doll layout inline.
	 */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Equipment")
	void ApplySlotCoordinates(const TMap<FGameplayTag, FIntPoint>& InCoordinates);

	/** The equipment container id this window is bound to (invalid until bound). */
	UFUNCTION(BlueprintPure, Category = "InvUI|Equipment")
	FInvUI_ContainerInstanceId GetEquipmentContainerId() const { return EquipmentContainerId; }

	/**
	 * Bind the equipment container to the equipment role AND install the paper-doll layout +
	 * show-empty on that role's grid VM (through the hosted grid's public VM getter). Call this
	 * instead of (or after) BindContainerToRole so the doll layout is applied without touching the
	 * window base's private BindRole.
	 */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Equipment")
	void BindEquipment(FInvUI_ContainerInstanceId EquipmentId);

	/** (Re)install the doll layout + SetShowEmptySlots on the equipment role's grid VM. */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Equipment")
	void ConfigureEquipmentGrid();

protected:
	//~ Begin UUserWidget
	virtual void NativeConstruct() override;
	//~ End UUserWidget

	//~ Begin UInvUI_WindowBase
	virtual FGameplayTag ResolveRoleForContainer_Implementation(FInvUI_ContainerInstanceId ContainerId, int32 OrderIndex) override;
	//~ End UInvUI_WindowBase

private:
	/** The equipment container id, captured during ResolveRoleForContainer for the comparison baseline. */
	UPROPERTY(Transient)
	FInvUI_ContainerInstanceId EquipmentContainerId;
};
