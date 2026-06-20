// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Subtitle/Loc_SubtitleViewModel.h"
#include "Core/DPLog.h"

namespace UE::FieldNotification
{
	/** Descriptor enumerating ULoc_SubtitleViewModel's observable fields by name. */
	struct FLoc_SubtitleViewModelDescriptor : public IClassDescriptor
	{
		static const FName FieldNames[(int32)ULoc_SubtitleViewModel::EField::Num];

		static FFieldId MakeId(ULoc_SubtitleViewModel::EField Field)
		{
			const int32 Index = (int32)Field;
			return FFieldId(FieldNames[Index], Index);
		}

		virtual void ForEachField(const UClass* /*Class*/, TFunctionRef<bool(FFieldId)> Callback) const override
		{
			for (int32 Index = 0; Index < (int32)ULoc_SubtitleViewModel::EField::Num; ++Index)
			{
				if (!Callback(FFieldId(FieldNames[Index], Index)))
				{
					break;
				}
			}
		}
	};

	const FName FLoc_SubtitleViewModelDescriptor::FieldNames[(int32)ULoc_SubtitleViewModel::EField::Num] =
	{
		FName(TEXT("VisibleSubtitles")),
		FName(TEXT("VisibleCount")),
		FName(TEXT("bSubtitlesEnabled")),
		FName(TEXT("SubtitleSize")),
		FName(TEXT("bBackground")),
	};

	static const FLoc_SubtitleViewModelDescriptor GLoc_SubtitleViewModelDescriptor;
}

const UE::FieldNotification::IClassDescriptor& ULoc_SubtitleViewModel::GetFieldNotificationDescriptor() const
{
	return UE::FieldNotification::GLoc_SubtitleViewModelDescriptor;
}

UE::FieldNotification::FFieldId ULoc_SubtitleViewModel::GetFieldId(EField Field)
{
	return UE::FieldNotification::FLoc_SubtitleViewModelDescriptor::MakeId(Field);
}

void ULoc_SubtitleViewModel::BroadcastField(EField Field)
{
	BroadcastFieldValueChanged(GetFieldId(Field));
}

void ULoc_SubtitleViewModel::SetVisibleSubtitles(const TArray<FLoc_ActiveSubtitleView>& InVisible)
{
	const int32 PreviousCount = VisibleSubtitles.Num();

	// The list always replaces wholesale; broadcast unconditionally since per-line TimeRemaining changes
	// frame-to-frame even when the count is stable.
	VisibleSubtitles = InVisible;
	BroadcastField(EField::VisibleSubtitles);

	if (PreviousCount != VisibleSubtitles.Num())
	{
		BroadcastField(EField::VisibleCount);
	}
}

void ULoc_SubtitleViewModel::SetAccessibilityPresentation(bool bInEnabled, ESeam_SubtitleSize InSize, bool bInBackground)
{
	if (bSubtitlesEnabled != bInEnabled)
	{
		bSubtitlesEnabled = bInEnabled;
		BroadcastField(EField::bSubtitlesEnabled);
	}

	if (SubtitleSize != InSize)
	{
		SubtitleSize = InSize;
		BroadcastField(EField::SubtitleSize);
	}

	if (bBackground != bInBackground)
	{
		bBackground = bInBackground;
		BroadcastField(EField::bBackground);
	}
}
