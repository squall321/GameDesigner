// Copyright DesignPatterns plugin. All Rights Reserved.

#include "MVVM/DPViewModelBase.h"
#include "Core/DPLog.h"
#include "UObject/UnrealType.h"

DECLARE_CYCLE_STAT(TEXT("DP MVVM BroadcastField"), STAT_DP_MVVM_Broadcast, STATGROUP_DesignPatterns);
DECLARE_DWORD_COUNTER_STAT(TEXT("DP MVVM Field Broadcasts"), STAT_DP_MVVM_BroadcastCount, STATGROUP_DesignPatterns);

namespace UE::FieldNotification
{
	/**
	 * Minimal class descriptor for the lite ViewModel base. The base class itself
	 * declares no FieldNotify fields; derived classes that use UE_FIELD_NOTIFICATION_*
	 * generate their own descriptor that chains to this one. We provide a concrete
	 * (empty) descriptor here so INotifyFieldValueChanged is fully implemented for
	 * the base type and never returns a dangling reference.
	 */
	struct FDP_ViewModelBaseDescriptor : public IClassDescriptor
	{
		virtual void ForEachField(const UClass* /*Class*/, TFunctionRef<bool(FFieldId)> /*Callback*/) const override
		{
			// Base declares no observable fields; derived descriptors enumerate theirs.
		}
	};

	static const FDP_ViewModelBaseDescriptor GDP_ViewModelBaseDescriptor;
}

const UE::FieldNotification::IClassDescriptor& UDP_ViewModelBase::GetFieldNotificationDescriptor() const
{
	return UE::FieldNotification::GDP_ViewModelBaseDescriptor;
}

FDelegateHandle UDP_ViewModelBase::AddFieldValueChangedDelegate(
	UE::FieldNotification::FFieldId InFieldId,
	FFieldValueChangedDelegate InNewDelegate)
{
	if (!InFieldId.IsValid())
	{
		return FDelegateHandle();
	}

	const FDelegateHandle Result = FieldNotifyDelegates.Add(this, InFieldId, MoveTemp(InNewDelegate));
	if (Result.IsValid())
	{
		EnabledFieldNotifications.PadToNum(InFieldId.GetIndex() + 1, false);
		EnabledFieldNotifications[InFieldId.GetIndex()] = true;
	}
	return Result;
}

bool UDP_ViewModelBase::RemoveFieldValueChangedDelegate(
	UE::FieldNotification::FFieldId InFieldId,
	FDelegateHandle InHandle)
{
	if (!InFieldId.IsValid() || !InHandle.IsValid())
	{
		return false;
	}

	const UE::FieldNotification::FFieldMulticastDelegate::FRemoveFromResult RemoveResult =
		FieldNotifyDelegates.RemoveFrom(this, InFieldId, InHandle);

	if (RemoveResult.bRemoved && !RemoveResult.bHasOtherBoundDelegates &&
		EnabledFieldNotifications.IsValidIndex(InFieldId.GetIndex()))
	{
		EnabledFieldNotifications[InFieldId.GetIndex()] = false;
	}
	return RemoveResult.bRemoved;
}

int32 UDP_ViewModelBase::RemoveAllFieldValueChangedDelegates(const void* InUserObject)
{
	const UE::FieldNotification::FFieldMulticastDelegate::FRemoveAllResult RemoveResult =
		FieldNotifyDelegates.RemoveAll(this, InUserObject);
	EnabledFieldNotifications = RemoveResult.HasFields;
	return RemoveResult.RemoveCount;
}

int32 UDP_ViewModelBase::RemoveAllFieldValueChangedDelegates(
	UE::FieldNotification::FFieldId InFieldId,
	const void* InUserObject)
{
	const UE::FieldNotification::FFieldMulticastDelegate::FRemoveAllResult RemoveResult =
		FieldNotifyDelegates.RemoveAll(this, InFieldId, InUserObject);
	EnabledFieldNotifications = RemoveResult.HasFields;
	return RemoveResult.RemoveCount;
}

void UDP_ViewModelBase::BroadcastFieldValueChanged(UE::FieldNotification::FFieldId InFieldId)
{
	SCOPE_CYCLE_COUNTER(STAT_DP_MVVM_Broadcast);
	INC_DWORD_STAT(STAT_DP_MVVM_BroadcastCount);

	if (!InFieldId.IsValid())
	{
		return;
	}

	if (EnabledFieldNotifications.IsValidIndex(InFieldId.GetIndex()) &&
		EnabledFieldNotifications[InFieldId.GetIndex()])
	{
		FieldNotifyDelegates.Broadcast(this, InFieldId);
	}
}

void UDP_ViewModelBase::K2_BroadcastFieldValueChanged(FName FieldName)
{
	// Resolve a FieldNotify property by name through the generated descriptor chain.
	const UE::FieldNotification::IClassDescriptor& Descriptor = GetFieldNotificationDescriptor();
	UE::FieldNotification::FFieldId Resolved;

	Descriptor.ForEachField(GetClass(), [&Resolved, FieldName](UE::FieldNotification::FFieldId Field)
	{
		if (Field.GetName() == FieldName)
		{
			Resolved = Field;
			return false; // stop iterating
		}
		return true;
	});

	if (!Resolved.IsValid())
	{
		UE_LOG(LogDP, Warning,
			TEXT("[MVVM] BroadcastFieldValueChanged: '%s' is not a FieldNotify field on %s."),
			*FieldName.ToString(), *GetClass()->GetName());
		return;
	}

	BroadcastFieldValueChanged(Resolved);
}
