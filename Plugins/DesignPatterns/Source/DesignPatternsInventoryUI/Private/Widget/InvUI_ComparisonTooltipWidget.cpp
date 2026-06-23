// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Widget/InvUI_ComparisonTooltipWidget.h"
#include "ViewModel/InvUI_ComparisonViewModel.h"
#include "INotifyFieldValueChanged.h"
#include "FieldNotification/IClassDescriptor.h"

void UInvUI_ComparisonTooltipWidget::BindComparison(UInvUI_ComparisonViewModel* InViewModel)
{
	UnbindComparison();

	ViewModel = InViewModel;
	if (ViewModel)
	{
		BindViewModelDelegates();
		OnComparisonRefreshed();
	}
}

void UInvUI_ComparisonTooltipWidget::UnbindComparison()
{
	UnbindViewModelDelegates();
	ViewModel = nullptr;
}

void UInvUI_ComparisonTooltipWidget::NativeDestruct()
{
	UnbindComparison();
	Super::NativeDestruct();
}

void UInvUI_ComparisonTooltipWidget::BindViewModelDelegates()
{
	if (bBoundToViewModel || !ViewModel)
	{
		return;
	}

	const UE::FieldNotification::IClassDescriptor& Descriptor = ViewModel->GetFieldNotificationDescriptor();

	INotifyFieldValueChanged::FFieldValueChangedDelegate Delegate =
		INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateUObject(
			this, &UInvUI_ComparisonTooltipWidget::HandleViewModelFieldChanged);

	Descriptor.ForEachField(ViewModel->GetClass(),
		[this, &Delegate](UE::FieldNotification::FFieldId Field)
		{
			ViewModel->AddFieldValueChangedDelegate(Field, Delegate);
			return true;
		});

	bBoundToViewModel = true;
}

void UInvUI_ComparisonTooltipWidget::UnbindViewModelDelegates()
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

void UInvUI_ComparisonTooltipWidget::HandleViewModelFieldChanged(UObject* /*Object*/, UE::FieldNotification::FFieldId /*FieldId*/)
{
	OnComparisonRefreshed();
}
