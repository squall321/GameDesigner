// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Widget/InvUI_SlotWidget.h"
#include "Widget/InvUI_DragDropOperation.h"
#include "Mediator/InvUI_ContainerMediatorComponent.h"

// Per-slot ViewModel carries the already-resolved display name/description/icon/quality, so the
// slot widget reads presentation straight off it and never touches a backend or display seam.
#include "ViewModel/InvUI_SlotViewModel.h"

#include "Components/Widget.h"
#include "Blueprint/DragDropOperation.h"
#include "INotifyFieldValueChanged.h"
#include "FieldNotification/IClassDescriptor.h"
#include "Core/DPLog.h"

void UInvUI_SlotWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// Default the drag trigger key once (FKey has no useful UPROPERTY default literal).
	if (!DragTriggerKey.IsValid())
	{
		DragTriggerKey = EKeys::LeftMouseButton;
	}

	BindViewModelDelegates();
	OnSlotRefreshed();
}

void UInvUI_SlotWidget::NativeDestruct()
{
	// Deterministic unbind so a recycled/torn-down slot never leaks a delegate onto its ViewModel.
	UnbindViewModelDelegates();
	Super::NativeDestruct();
}

void UInvUI_SlotWidget::BindSlot(UInvUI_SlotViewModel* InViewModel,
	FInvUI_ContainerInstanceId InContainerId,
	FGameplayTag InSlotTag,
	UInvUI_ContainerMediatorComponent* InMediator)
{
	// Release the previous ViewModel's binding before swapping so we never double-bind.
	UnbindViewModelDelegates();

	SlotViewModel = InViewModel;
	ContainerId = InContainerId;
	SlotTag = InSlotTag;
	Mediator = InMediator;

	// Only bind delegates once the widget is constructed; NativeConstruct will bind otherwise.
	if (IsConstructed())
	{
		BindViewModelDelegates();
		OnSlotRefreshed();
	}
}

void UInvUI_SlotWidget::UnbindSlot()
{
	UnbindViewModelDelegates();
	SlotViewModel = nullptr;
	Mediator = nullptr;
	ContainerId = FInvUI_ContainerInstanceId();
	SlotTag = FGameplayTag();

	if (IsConstructed())
	{
		OnSlotRefreshed();
	}
}

bool UInvUI_SlotWidget::IsEmpty() const
{
	return !SlotViewModel || SlotViewModel->IsEmpty();
}

void UInvUI_SlotWidget::BindViewModelDelegates()
{
	if (bBoundToViewModel || !SlotViewModel)
	{
		return;
	}

	// Bind a single delegate to every observable field exposed by the ViewModel's class descriptor.
	const UE::FieldNotification::IClassDescriptor& Descriptor = SlotViewModel->GetFieldNotificationDescriptor();

	INotifyFieldValueChanged::FFieldValueChangedDelegate Delegate =
		INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateUObject(
			this, &UInvUI_SlotWidget::HandleViewModelFieldChanged);

	Descriptor.ForEachField(SlotViewModel->GetClass(),
		[this, &Delegate](UE::FieldNotification::FFieldId Field)
		{
			SlotViewModel->AddFieldValueChangedDelegate(Field, Delegate);
			return true; // continue
		});

	bBoundToViewModel = true;
}

void UInvUI_SlotWidget::UnbindViewModelDelegates()
{
	if (!bBoundToViewModel)
	{
		return;
	}

	if (SlotViewModel)
	{
		SlotViewModel->RemoveAllFieldValueChangedDelegates(this);
	}
	FieldChangedHandle.Reset();
	bBoundToViewModel = false;
}

void UInvUI_SlotWidget::HandleViewModelFieldChanged(UObject* /*Object*/, UE::FieldNotification::FFieldId /*FieldId*/)
{
	// Any field change re-projects the slot. Slots are cheap; a targeted re-read is not worth
	// the per-field dispatch complexity here.
	OnSlotRefreshed();
}

void UInvUI_SlotWidget::NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent,
	UDragDropOperation*& OutOperation)
{
	OutOperation = nullptr;

	// Nothing to drag from an empty cell.
	if (!SlotViewModel || SlotViewModel->IsEmpty() || !ContainerId.IsValid() || !SlotTag.IsValid())
	{
		return;
	}

	const FGameplayTag ItemTag = SlotViewModel->GetItemTag();
	const int32 Count = SlotViewModel->GetCount();
	if (!ItemTag.IsValid() || Count < 1)
	{
		return;
	}

	UInvUI_DragDropOperation* Op = UInvUI_DragDropOperation::MakeSlotDrag(ContainerId, SlotTag, ItemTag, Count);

	// Let the designer build the drag-cursor visual; attach it if provided.
	if (UUserWidget* Visual = MakeDragVisual(Op))
	{
		Op->DefaultDragVisual = Visual;
		Op->Pivot = EDragPivot::MouseDown;
	}

	OutOperation = Op;
	UE_LOG(LogDP, Verbose, TEXT("[InvUI] Slot %s started drag: %s"), *SlotTag.ToString(), *Op->ToDebugString());
}

bool UInvUI_SlotWidget::NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent,
	UDragDropOperation* InOperation)
{
	// Clear any hover highlight regardless of outcome.
	if (bDropHighlighted)
	{
		bDropHighlighted = false;
		OnSetHighlighted(false, false);
	}

	UInvUI_DragDropOperation* DragOp = Cast<UInvUI_DragDropOperation>(InOperation);
	if (!DragOp || !DragOp->IsValidDrag())
	{
		return false;
	}

	if (!ContainerId.IsValid() || !SlotTag.IsValid())
	{
		return false;
	}

	UInvUI_ContainerMediatorComponent* Med = Mediator.Get();
	if (!Med)
	{
		UE_LOG(LogDP, Warning, TEXT("[InvUI] Drop on slot %s with no mediator bound."), *SlotTag.ToString());
		return false;
	}

	// Client only REQUESTS a move by identity. The mediator forwards to the server, which
	// re-resolves both containers from the registry and re-validates before mutating state.
	Med->RequestMoveByIdentity(
		DragOp->SourceContainerId, DragOp->SourceSlotTag,
		ContainerId, SlotTag,
		DragOp->Count);

	UE_LOG(LogDP, Verbose, TEXT("[InvUI] Slot %s accepted drop: %s -> %s::%s"),
		*SlotTag.ToString(), *DragOp->ToDebugString(), *ContainerId.ToString(), *SlotTag.ToString());
	return true;
}

void UInvUI_SlotWidget::NativeOnDragEnter(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent,
	UDragDropOperation* InOperation)
{
	Super::NativeOnDragEnter(InGeometry, InDragDropEvent, InOperation);

	const UInvUI_DragDropOperation* DragOp = Cast<UInvUI_DragDropOperation>(InOperation);
	const bool bValidTarget = DragOp && DragOp->IsValidDrag() && ContainerId.IsValid() && SlotTag.IsValid();

	bDropHighlighted = true;
	OnSetHighlighted(true, bValidTarget);
}

void UInvUI_SlotWidget::NativeOnDragLeave(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	Super::NativeOnDragLeave(InDragDropEvent, InOperation);

	if (bDropHighlighted)
	{
		bDropHighlighted = false;
		OnSetHighlighted(false, false);
	}
}

void UInvUI_SlotWidget::NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseEnter(InGeometry, InMouseEvent);
	OnSetHovered(true);
}

void UInvUI_SlotWidget::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseLeave(InMouseEvent);
	OnSetHovered(false);
}

FText UInvUI_SlotWidget::GetTooltipTitle() const
{
	if (IsEmpty() || !SlotViewModel)
	{
		return FText::GetEmpty();
	}
	return SlotViewModel->GetDisplayName();
}

FText UInvUI_SlotWidget::GetTooltipBody() const
{
	if (IsEmpty() || !SlotViewModel)
	{
		return FText::GetEmpty();
	}
	return SlotViewModel->GetDescription();
}

UTexture2D* UInvUI_SlotWidget::GetSlotIcon() const
{
	if (IsEmpty() || !SlotViewModel)
	{
		return nullptr;
	}
	return SlotViewModel->GetIcon();
}

FLinearColor UInvUI_SlotWidget::GetSlotQualityColor() const
{
	return SlotViewModel ? SlotViewModel->GetQualityColor() : FLinearColor::White;
}
