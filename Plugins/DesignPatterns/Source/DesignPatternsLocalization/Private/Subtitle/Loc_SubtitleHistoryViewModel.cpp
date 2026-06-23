// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Subtitle/Loc_SubtitleHistoryViewModel.h"

namespace UE::FieldNotification
{
	/** Descriptor enumerating ULoc_SubtitleHistoryViewModel's observable fields by name. */
	struct FLoc_SubtitleHistoryViewModelDescriptor : public IClassDescriptor
	{
		static const FName FieldNames[(int32)ULoc_SubtitleHistoryViewModel::EField::Num];

		static FFieldId MakeId(ULoc_SubtitleHistoryViewModel::EField Field)
		{
			const int32 Index = (int32)Field;
			return FFieldId(FieldNames[Index], Index);
		}

		virtual void ForEachField(const UClass* /*Class*/, TFunctionRef<bool(FFieldId)> Callback) const override
		{
			for (int32 Index = 0; Index < (int32)ULoc_SubtitleHistoryViewModel::EField::Num; ++Index)
			{
				if (!Callback(FFieldId(FieldNames[Index], Index)))
				{
					break;
				}
			}
		}
	};

	const FName FLoc_SubtitleHistoryViewModelDescriptor::FieldNames[(int32)ULoc_SubtitleHistoryViewModel::EField::Num] =
	{
		FName(TEXT("HistoryEntries")),
		FName(TEXT("UnreadCount")),
	};

	static const FLoc_SubtitleHistoryViewModelDescriptor GLoc_SubtitleHistoryViewModelDescriptor;
}

const UE::FieldNotification::IClassDescriptor& ULoc_SubtitleHistoryViewModel::GetFieldNotificationDescriptor() const
{
	return UE::FieldNotification::GLoc_SubtitleHistoryViewModelDescriptor;
}

UE::FieldNotification::FFieldId ULoc_SubtitleHistoryViewModel::GetFieldId(EField Field)
{
	return UE::FieldNotification::FLoc_SubtitleHistoryViewModelDescriptor::MakeId(Field);
}

void ULoc_SubtitleHistoryViewModel::BroadcastField(EField Field)
{
	BroadcastFieldValueChanged(GetFieldId(Field));
}

void ULoc_SubtitleHistoryViewModel::SetHistory(const TArray<FLoc_SubtitleHistoryEntry>& InEntries, int32 InUnreadCount)
{
	HistoryEntries = InEntries;
	BroadcastField(EField::HistoryEntries);

	if (UnreadCount != InUnreadCount)
	{
		UnreadCount = InUnreadCount;
		BroadcastField(EField::UnreadCount);
	}
}

void ULoc_SubtitleHistoryViewModel::MarkAllRead()
{
	if (UnreadCount != 0)
	{
		UnreadCount = 0;
		BroadcastField(EField::UnreadCount);
	}
}
