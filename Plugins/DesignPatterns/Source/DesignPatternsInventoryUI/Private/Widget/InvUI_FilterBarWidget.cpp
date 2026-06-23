// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Widget/InvUI_FilterBarWidget.h"
#include "ViewModel/InvUI_FilterBarViewModel.h"
#include "INotifyFieldValueChanged.h"
#include "FieldNotification/IClassDescriptor.h"

void UInvUI_FilterBarWidget::BindFilterBar(UInvUI_FilterBarViewModel* InViewModel)
{
	UnbindFilterBar();

	ViewModel = InViewModel;
	if (ViewModel)
	{
		BindViewModelDelegates();
		OnFilterBarRefreshed();
	}
}

void UInvUI_FilterBarWidget::UnbindFilterBar()
{
	UnbindViewModelDelegates();
	ViewModel = nullptr;
}

void UInvUI_FilterBarWidget::NativeDestruct()
{
	UnbindFilterBar();
	Super::NativeDestruct();
}

void UInvUI_FilterBarWidget::SetSearchText(FText InSearchText)
{
	if (ViewModel) { ViewModel->SetSearchText(InSearchText); }
}

void UInvUI_FilterBarWidget::SetTypeFilter(const FGameplayTagContainer& InFilter)
{
	if (ViewModel) { ViewModel->SetTypeFilter(InFilter); }
}

void UInvUI_FilterBarWidget::SetSortMode(FGameplayTag InSortMode)
{
	if (ViewModel) { ViewModel->SetSortMode(InSortMode); }
}

void UInvUI_FilterBarWidget::SetShowEmpty(bool bInShowEmpty)
{
	if (ViewModel) { ViewModel->SetShowEmpty(bInShowEmpty); }
}

void UInvUI_FilterBarWidget::BindViewModelDelegates()
{
	if (bBoundToViewModel || !ViewModel)
	{
		return;
	}

	const UE::FieldNotification::IClassDescriptor& Descriptor = ViewModel->GetFieldNotificationDescriptor();

	INotifyFieldValueChanged::FFieldValueChangedDelegate Delegate =
		INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateUObject(
			this, &UInvUI_FilterBarWidget::HandleViewModelFieldChanged);

	Descriptor.ForEachField(ViewModel->GetClass(),
		[this, &Delegate](UE::FieldNotification::FFieldId Field)
		{
			ViewModel->AddFieldValueChangedDelegate(Field, Delegate);
			return true;
		});

	bBoundToViewModel = true;
}

void UInvUI_FilterBarWidget::UnbindViewModelDelegates()
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

void UInvUI_FilterBarWidget::HandleViewModelFieldChanged(UObject* /*Object*/, UE::FieldNotification::FFieldId /*FieldId*/)
{
	OnFilterBarRefreshed();
}
