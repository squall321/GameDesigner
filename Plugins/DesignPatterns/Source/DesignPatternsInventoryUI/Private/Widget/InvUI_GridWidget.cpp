// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Widget/InvUI_GridWidget.h"
#include "Widget/InvUI_SlotWidget.h"
#include "Mediator/InvUI_ContainerMediatorComponent.h"

#include "ViewModel/InvUI_GridViewModel.h"
#include "ViewModel/InvUI_SlotViewModel.h"

#include "Components/PanelWidget.h"
#include "INotifyFieldValueChanged.h"
#include "FieldNotification/IClassDescriptor.h"
#include "Core/DPLog.h"

void UInvUI_GridWidget::NativeConstruct()
{
	Super::NativeConstruct();
	BindViewModelDelegates();
	RebuildSlots();
}

void UInvUI_GridWidget::NativeDestruct()
{
	UnbindViewModelDelegates();
	ClearSlotWidgets();
	Super::NativeDestruct();
}

void UInvUI_GridWidget::BindGrid(UInvUI_GridViewModel* InViewModel, UInvUI_ContainerMediatorComponent* InMediator)
{
	UnbindViewModelDelegates();

	GridViewModel = InViewModel;
	Mediator = InMediator;
	LastStructureRevision = -1;

	if (IsConstructed())
	{
		BindViewModelDelegates();
		RebuildSlots();
	}
}

void UInvUI_GridWidget::UnbindGrid()
{
	UnbindViewModelDelegates();
	ClearSlotWidgets();
	GridViewModel = nullptr;
	Mediator = nullptr;
	LastStructureRevision = -1;
}

FInvUI_ContainerInstanceId UInvUI_GridWidget::GetContainerId() const
{
	return GridViewModel ? GridViewModel->GetContainerId() : FInvUI_ContainerInstanceId();
}

void UInvUI_GridWidget::BindViewModelDelegates()
{
	if (bBoundToViewModel || !GridViewModel)
	{
		return;
	}

	const UE::FieldNotification::IClassDescriptor& Descriptor = GridViewModel->GetFieldNotificationDescriptor();

	INotifyFieldValueChanged::FFieldValueChangedDelegate Delegate =
		INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateUObject(
			this, &UInvUI_GridWidget::HandleViewModelFieldChanged);

	Descriptor.ForEachField(GridViewModel->GetClass(),
		[this, &Delegate](UE::FieldNotification::FFieldId Field)
		{
			GridViewModel->AddFieldValueChangedDelegate(Field, Delegate);
			return true; // continue
		});

	bBoundToViewModel = true;
}

void UInvUI_GridWidget::UnbindViewModelDelegates()
{
	if (!bBoundToViewModel)
	{
		return;
	}
	if (GridViewModel)
	{
		GridViewModel->RemoveAllFieldValueChangedDelegates(this);
	}
	bBoundToViewModel = false;
}

void UInvUI_GridWidget::HandleViewModelFieldChanged(UObject* /*Object*/, UE::FieldNotification::FFieldId FieldId)
{
	if (!GridViewModel)
	{
		return;
	}

	// Only a coarse structure change (slot set / order / layout extent) requires rebuilding the
	// child widget set. Per-slot icon/count changes are observed directly by each slot widget on
	// its own slot ViewModel, so they never reach here as a structural concern.
	const UE::FieldNotification::FFieldId StructureField =
		UInvUI_GridViewModel::GetFieldId(UInvUI_GridViewModel::EField::StructureRevision);
	const UE::FieldNotification::FFieldId SlotCountField =
		UInvUI_GridViewModel::GetFieldId(UInvUI_GridViewModel::EField::SlotCount);
	const UE::FieldNotification::FFieldId BoundField =
		UInvUI_GridViewModel::GetFieldId(UInvUI_GridViewModel::EField::bBound);

	if (FieldId == StructureField || FieldId == SlotCountField || FieldId == BoundField)
	{
		RebuildSlots();
	}
}

void UInvUI_GridWidget::RebuildSlots()
{
	UPanelWidget* Panel = ResolveSlotContainer();
	if (!Panel)
	{
		UE_LOG(LogDP, Warning, TEXT("[InvUI] Grid '%s' has no slot container panel; cannot build slots."),
			*GetName());
		ClearSlotWidgets();
		return;
	}
	if (!SlotWidgetClass)
	{
		UE_LOG(LogDP, Warning, TEXT("[InvUI] Grid '%s' has no SlotWidgetClass set."), *GetName());
		ClearSlotWidgets();
		return;
	}

	if (!GridViewModel)
	{
		ClearSlotWidgets();
		return;
	}

	// Skip redundant rebuilds for an unchanged structure (the same revision may broadcast more than
	// once when several coarse fields change together).
	const int32 Revision = GridViewModel->GetStructureRevision();
	const TArray<UInvUI_SlotViewModel*>& SlotVMs = GridViewModel->GetSlotViewModels();
	if (Revision == LastStructureRevision && SlotVMs.Num() == SlotWidgets.Num())
	{
		return;
	}
	LastStructureRevision = Revision;

	const FInvUI_ContainerInstanceId Id = GridViewModel->GetContainerId();
	const int32 DesiredCount = SlotVMs.Num();

	// Trim surplus pooled widgets when the displayed set shrank.
	while (SlotWidgets.Num() > DesiredCount)
	{
		const int32 Last = SlotWidgets.Num() - 1;
		if (UInvUI_SlotWidget* SlotWidget = SlotWidgets[Last])
		{
			SlotWidget->UnbindSlot();
			SlotWidget->RemoveFromParent();
		}
		SlotWidgets.RemoveAt(Last);
	}

	// Grow / re-bind to the desired count, reusing existing children where possible.
	for (int32 Index = 0; Index < DesiredCount; ++Index)
	{
		UInvUI_SlotViewModel* SlotVM = SlotVMs[Index];

		UInvUI_SlotWidget* SlotWidget = SlotWidgets.IsValidIndex(Index) ? SlotWidgets[Index].Get() : nullptr;
		const bool bNewWidget = (SlotWidget == nullptr);
		if (bNewWidget)
		{
			SlotWidget = CreateWidget<UInvUI_SlotWidget>(this, SlotWidgetClass);
			if (!SlotWidget)
			{
				UE_LOG(LogDP, Error, TEXT("[InvUI] Failed to create slot widget for grid '%s' index %d."),
					*GetName(), Index);
				continue;
			}
			if (SlotWidgets.IsValidIndex(Index))
			{
				SlotWidgets[Index] = SlotWidget;
			}
			else
			{
				SlotWidgets.SetNum(Index + 1);
				SlotWidgets[Index] = SlotWidget;
			}
			Panel->AddChild(SlotWidget);
		}

		const FGameplayTag SlotTag = SlotVM ? SlotVM->GetSlotTag() : FGameplayTag();
		SlotWidget->BindSlot(SlotVM, Id, SlotTag, Mediator.Get());

		const int32 Column = SlotVM ? SlotVM->GetColumn() : 0;
		const int32 Row = SlotVM ? SlotVM->GetRow() : 0;
		OnSlotWidgetPlaced(SlotWidget, Column, Row);
	}

	OnGridRebuilt(GridViewModel->GetColumnCount(), GridViewModel->GetRowCount());
}

void UInvUI_GridWidget::ClearSlotWidgets()
{
	for (TObjectPtr<UInvUI_SlotWidget>& SlotWidget : SlotWidgets)
	{
		if (SlotWidget)
		{
			SlotWidget->UnbindSlot();
			SlotWidget->RemoveFromParent();
		}
	}
	SlotWidgets.Reset();
}

UPanelWidget* UInvUI_GridWidget::ResolveSlotContainer_Implementation()
{
	return SlotContainer;
}

void UInvUI_GridWidget::RequestSplitStack(FGameplayTag SlotTag, int32 SplitCount)
{
	if (!GridViewModel || !SlotTag.IsValid() || SplitCount < 1)
	{
		return;
	}
	UInvUI_ContainerMediatorComponent* Med = Mediator.Get();
	if (!Med)
	{
		return;
	}

	const FInvUI_ContainerInstanceId Id = GridViewModel->GetContainerId();

	// A split moves SplitCount units back into the SAME container with an UNSPECIFIED destination
	// slot (empty ToSlot): the server picks the first free slot and re-validates the amount.
	Med->RequestMoveByIdentity(Id, SlotTag, Id, FGameplayTag(), SplitCount);

	UE_LOG(LogDP, Verbose, TEXT("[InvUI] Grid requested split: %s::%s x%d"),
		*Id.ToString(), *SlotTag.ToString(), SplitCount);
}

void UInvUI_GridWidget::RequestMergeStacks(FGameplayTag FromSlotTag, FGameplayTag ToSlotTag)
{
	if (!GridViewModel || !FromSlotTag.IsValid() || !ToSlotTag.IsValid() || FromSlotTag == ToSlotTag)
	{
		return;
	}
	UInvUI_ContainerMediatorComponent* Med = Mediator.Get();
	if (!Med)
	{
		return;
	}

	const FInvUI_ContainerInstanceId Id = GridViewModel->GetContainerId();

	// A merge is a full-stack move within the same container; Count 0 means "the whole source
	// stack" — the server re-derives the amount and the backend coalesces on matching item tags.
	Med->RequestMoveByIdentity(Id, FromSlotTag, Id, ToSlotTag, 0);

	UE_LOG(LogDP, Verbose, TEXT("[InvUI] Grid requested merge: %s::%s -> %s::%s"),
		*Id.ToString(), *FromSlotTag.ToString(), *Id.ToString(), *ToSlotTag.ToString());
}
