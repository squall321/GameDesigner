// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Notification/HUD_NotificationViewModel.h"
#include "Core/DPLog.h"

namespace UE::FieldNotification
{
	/** Descriptor enumerating UHUD_NotificationViewModel's observable fields by name. */
	struct FHUD_NotificationViewModelDescriptor : public IClassDescriptor
	{
		static const FName FieldNames[(int32)UHUD_NotificationViewModel::EField::Num];

		static FFieldId MakeId(UHUD_NotificationViewModel::EField Field)
		{
			const int32 Index = (int32)Field;
			return FFieldId(FieldNames[Index], Index);
		}

		virtual void ForEachField(const UClass* /*Class*/, TFunctionRef<bool(FFieldId)> Callback) const override
		{
			for (int32 Index = 0; Index < (int32)UHUD_NotificationViewModel::EField::Num; ++Index)
			{
				if (!Callback(FFieldId(FieldNames[Index], Index)))
				{
					break;
				}
			}
		}
	};

	const FName FHUD_NotificationViewModelDescriptor::FieldNames[(int32)UHUD_NotificationViewModel::EField::Num] =
	{
		FName(TEXT("VisibleNotifications")),
		FName(TEXT("VisibleCount")),
		FName(TEXT("QueuedCount")),
	};

	static const FHUD_NotificationViewModelDescriptor GHUD_NotificationViewModelDescriptor;
}

const UE::FieldNotification::IClassDescriptor& UHUD_NotificationViewModel::GetFieldNotificationDescriptor() const
{
	return UE::FieldNotification::GHUD_NotificationViewModelDescriptor;
}

UE::FieldNotification::FFieldId UHUD_NotificationViewModel::GetFieldId(EField Field)
{
	return UE::FieldNotification::FHUD_NotificationViewModelDescriptor::MakeId(Field);
}

void UHUD_NotificationViewModel::BroadcastField(EField Field)
{
	BroadcastFieldValueChanged(GetFieldId(Field));
}

void UHUD_NotificationViewModel::SetVisibleNotifications(const TArray<FHUD_ActiveNotificationView>& InVisible)
{
	const int32 PreviousCount = VisibleNotifications.Num();

	// The list always replaces wholesale; broadcast unconditionally since per-item time updates mean
	// the contents change frame-to-frame even when the count is stable.
	VisibleNotifications = InVisible;
	BroadcastField(EField::VisibleNotifications);

	if (PreviousCount != VisibleNotifications.Num())
	{
		BroadcastField(EField::VisibleCount);
	}
}

void UHUD_NotificationViewModel::SetQueuedCount(int32 InQueuedCount)
{
	if (QueuedCount != InQueuedCount)
	{
		QueuedCount = InQueuedCount;
		BroadcastField(EField::QueuedCount);
	}
}
