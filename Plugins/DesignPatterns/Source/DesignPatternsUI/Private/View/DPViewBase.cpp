// Copyright DesignPatterns plugin. All Rights Reserved.

#include "View/DPViewBase.h"
#include "MVVM/DPViewModelBase.h"
#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "FieldNotification/FieldId.h"
#include "FieldNotification/IClassDescriptor.h"

DECLARE_CYCLE_STAT(TEXT("DP View Bind"), STAT_DP_View_Bind, STATGROUP_DesignPatterns);
DECLARE_DWORD_COUNTER_STAT(TEXT("DP View Intents Published"), STAT_DP_View_Intents, STATGROUP_DesignPatterns);

void UDP_ViewBase::SetViewModel(UDP_ViewModelBase* InViewModel)
{
	if (ViewModel == InViewModel)
	{
		return;
	}

	// Tear down bindings to the outgoing ViewModel before swapping.
	if (bBoundToViewModel)
	{
		UnbindFromViewModel();
	}

	ViewModel = InViewModel;

	// Only bind live if the widget is actually constructed; otherwise NativeConstruct binds later.
	if (ViewModel && IsConstructed())
	{
		BindToViewModel();
	}

	if (ViewModel)
	{
		OnViewModelSet(ViewModel);
	}
}

void UDP_ViewBase::NativeConstruct()
{
	Super::NativeConstruct();

	if (ViewModel && !bBoundToViewModel)
	{
		BindToViewModel();
		// Re-fire the designer hook on (re)construct so widgets re-read initial state.
		OnViewModelSet(ViewModel);
	}
}

void UDP_ViewBase::NativeDestruct()
{
	// Deterministic unbind: this is the primary teardown path.
	UnbindFromViewModel();
	Super::NativeDestruct();
}

void UDP_ViewBase::BeginDestroy()
{
	// Backstop: a widget collected without a NativeDestruct still releases bindings.
	UnbindFromViewModel();
	Super::BeginDestroy();
}

void UDP_ViewBase::BindToViewModel()
{
	SCOPE_CYCLE_COUNTER(STAT_DP_View_Bind);

	if (bBoundToViewModel || !ViewModel)
	{
		return;
	}

	// Bind a single delegate to every observable field exposed by the ViewModel's class descriptor.
	const UE::FieldNotification::IClassDescriptor& Descriptor = ViewModel->GetFieldNotificationDescriptor();

	INotifyFieldValueChanged::FFieldValueChangedDelegate Delegate =
		INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateUObject(this, &UDP_ViewBase::HandleFieldValueChanged);

	Descriptor.ForEachField(ViewModel->GetClass(), [this, &Delegate](UE::FieldNotification::FFieldId Field)
	{
		ViewModel->AddFieldValueChangedDelegate(Field, Delegate);
		return true; // continue
	});

	bBoundToViewModel = true;
}

void UDP_ViewBase::UnbindFromViewModel()
{
	if (!bBoundToViewModel)
	{
		return;
	}

	if (ViewModel)
	{
		// Removing by user object drops every field delegate this view registered.
		ViewModel->RemoveAllFieldValueChangedDelegates(this);
	}

	bBoundToViewModel = false;
}

void UDP_ViewBase::HandleFieldValueChanged(UObject* Object, UE::FieldNotification::FFieldId FieldId)
{
	if (!FieldId.IsValid())
	{
		return;
	}
	OnViewModelFieldChanged(FieldId.GetName());
}

void UDP_ViewBase::OnViewModelFieldChanged_Implementation(FName /*FieldName*/)
{
	// Default: no-op. Designers/native subclasses override to re-read specific fields.
}

void UDP_ViewBase::PublishIntent(FGameplayTag IntentChannel, FInstancedStruct Payload)
{
	if (!IntentChannel.IsValid())
	{
		UE_LOG(LogDP, Warning, TEXT("[View] PublishIntent called with an invalid channel tag from %s."),
			*GetName());
		return;
	}

	UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this);
	if (!Bus)
	{
		UE_LOG(LogDP, Verbose, TEXT("[View] PublishIntent: message bus unavailable (design-time?); dropping %s."),
			*IntentChannel.ToString());
		return;
	}

	INC_DWORD_STAT(STAT_DP_View_Intents);

	// The view is the instigator so listeners can attribute the intent to a specific widget.
	Bus->BroadcastPayload(IntentChannel, Payload, this);
}
