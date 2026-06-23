// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Widget/InvUI_QuantitySplitterWidget.h"
#include "ViewModel/InvUI_QuantitySplitterViewModel.h"
#include "INotifyFieldValueChanged.h"
#include "FieldNotification/IClassDescriptor.h"

void UInvUI_QuantitySplitterWidget::BindSplitter(UInvUI_QuantitySplitterViewModel* InViewModel)
{
	UnbindSplitter();

	ViewModel = InViewModel;
	if (ViewModel)
	{
		BindViewModelDelegates();
		OnSplitterRefreshed();
	}
}

void UInvUI_QuantitySplitterWidget::UnbindSplitter()
{
	UnbindViewModelDelegates();
	ViewModel = nullptr;
}

void UInvUI_QuantitySplitterWidget::NativeDestruct()
{
	UnbindSplitter();
	Super::NativeDestruct();
}

void UInvUI_QuantitySplitterWidget::SetSelected(int32 InSelected)
{
	if (ViewModel)
	{
		ViewModel->SetSelected(InSelected);
	}
}

void UInvUI_QuantitySplitterWidget::Confirm(UInvUI_ContainerMediatorComponent* Mediator,
	FInvUI_ContainerInstanceId ToContainer)
{
	if (ViewModel)
	{
		ViewModel->ConfirmSplit(Mediator, ToContainer);
	}
}

void UInvUI_QuantitySplitterWidget::BindViewModelDelegates()
{
	if (bBoundToViewModel || !ViewModel)
	{
		return;
	}

	const UE::FieldNotification::IClassDescriptor& Descriptor = ViewModel->GetFieldNotificationDescriptor();

	INotifyFieldValueChanged::FFieldValueChangedDelegate Delegate =
		INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateUObject(
			this, &UInvUI_QuantitySplitterWidget::HandleViewModelFieldChanged);

	Descriptor.ForEachField(ViewModel->GetClass(),
		[this, &Delegate](UE::FieldNotification::FFieldId Field)
		{
			ViewModel->AddFieldValueChangedDelegate(Field, Delegate);
			return true;
		});

	bBoundToViewModel = true;
}

void UInvUI_QuantitySplitterWidget::UnbindViewModelDelegates()
{
	if (!bBoundToViewModel)
	{
		return;
	}
	if (ViewModel)
	{
		ViewModel->RemoveAllFieldValueChangedDelegates(this);
	}
	bBoundToViewModel = false;
}

void UInvUI_QuantitySplitterWidget::HandleViewModelFieldChanged(UObject* /*Object*/, UE::FieldNotification::FFieldId /*FieldId*/)
{
	OnSplitterRefreshed();
}
