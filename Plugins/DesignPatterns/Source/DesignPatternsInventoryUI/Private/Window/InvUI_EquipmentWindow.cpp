// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Window/InvUI_EquipmentWindow.h"
#include "Widget/InvUI_GridWidget.h"
#include "ViewModel/InvUI_GridViewModel.h"
#include "InvUI_NativeTags.h"
#include "Core/DPLog.h"

UInvUI_EquipmentWindow::UInvUI_EquipmentWindow()
{
	// Default the equipment role to the canonical equipment screen tag so a window asset that does
	// not override it still resolves a sensible role. Designers can retarget EquipmentRole freely.
	EquipmentRole = InvUITags::Screen_Equipment;
}

void UInvUI_EquipmentWindow::NativeConstruct()
{
	Super::NativeConstruct();

	// Ensure a doll layout exists so the equipment grid always has named-slot placement, even before
	// the designer authors SlotCoordinates (an empty doll simply lays out nothing).
	if (DollLayout == nullptr)
	{
		DollLayout = NewObject<UInvUI_PaperDollLayout>(this);
	}
}

void UInvUI_EquipmentWindow::ApplySlotCoordinates(const TMap<FGameplayTag, FIntPoint>& InCoordinates)
{
	if (DollLayout == nullptr)
	{
		DollLayout = NewObject<UInvUI_PaperDollLayout>(this);
	}
	for (const TPair<FGameplayTag, FIntPoint>& Pair : InCoordinates)
	{
		DollLayout->SlotCoordinates.Add(Pair.Key, Pair.Value);
	}
}

FGameplayTag UInvUI_EquipmentWindow::ResolveRoleForContainer_Implementation(
	FInvUI_ContainerInstanceId ContainerId, int32 OrderIndex)
{
	// A single equipment window maps its one container to the equipment role and remembers the id
	// for the comparison baseline. Fall back to the base policy if no equipment role is set.
	if (EquipmentRole.IsValid())
	{
		EquipmentContainerId = ContainerId;
		return EquipmentRole;
	}
	return Super::ResolveRoleForContainer_Implementation(ContainerId, OrderIndex);
}

void UInvUI_EquipmentWindow::BindEquipment(FInvUI_ContainerInstanceId EquipmentId)
{
	EquipmentContainerId = EquipmentId;
	BindContainerToRole(EquipmentRole, EquipmentId); // base resolves + binds the role's grid VM
	ConfigureEquipmentGrid();
}

void UInvUI_EquipmentWindow::ConfigureEquipmentGrid()
{
	// Reach the equipment role's grid widget (HostedGrids is protected on the base) and install the
	// doll layout + show-empty via the grid's PUBLIC ViewModel getter. This does not touch the base's
	// private BindRole — it layers configuration on top of the already-bound grid VM.
	TObjectPtr<UInvUI_GridWidget>* GridPtr = HostedGrids.Find(EquipmentRole);
	if (!GridPtr || !*GridPtr)
	{
		UE_LOG(LogDP, Warning, TEXT("[InvUI] EquipmentWindow '%s' has no hosted grid for role %s."),
			*GetName(), *EquipmentRole.ToString());
		return;
	}

	UInvUI_GridViewModel* GridVM = (*GridPtr)->GetGridViewModel();
	if (GridVM == nullptr)
	{
		// The grid is not bound yet (container not registered): nothing to configure until BindRole runs.
		return;
	}

	if (DollLayout == nullptr)
	{
		DollLayout = NewObject<UInvUI_PaperDollLayout>(this);
	}

	GridVM->SetShowEmptySlots(true);          // equipment shows every named slot, full or empty
	GridVM->SetLayoutStrategy(DollLayout);    // designer-authored slot -> cell placement (rebuilds)
}
