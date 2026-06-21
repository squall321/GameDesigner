// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Loading/Flow_LoadingViewModel.h"
#include "Core/DPLog.h"

namespace UE::FieldNotification
{
	/** Descriptor enumerating UFlow_LoadingViewModel's observable fields by name. */
	struct FFlow_LoadingViewModelDescriptor : public IClassDescriptor
	{
		static const FName FieldNames[(int32)UFlow_LoadingViewModel::EField::Num];

		static FFieldId MakeId(UFlow_LoadingViewModel::EField Field)
		{
			const int32 Index = (int32)Field;
			return FFieldId(FieldNames[Index], Index);
		}

		virtual void ForEachField(const UClass* /*Class*/, TFunctionRef<bool(FFieldId)> Callback) const override
		{
			for (int32 Index = 0; Index < (int32)UFlow_LoadingViewModel::EField::Num; ++Index)
			{
				if (!Callback(FFieldId(FieldNames[Index], Index)))
				{
					break;
				}
			}
		}
	};

	const FName FFlow_LoadingViewModelDescriptor::FieldNames[(int32)UFlow_LoadingViewModel::EField::Num] =
	{
		FName(TEXT("Progress")),
		FName(TEXT("StatusLabel")),
		FName(TEXT("bIndeterminate")),
		FName(TEXT("bVisible")),
	};

	static const FFlow_LoadingViewModelDescriptor GFlow_LoadingViewModelDescriptor;
}

const UE::FieldNotification::IClassDescriptor& UFlow_LoadingViewModel::GetFieldNotificationDescriptor() const
{
	return UE::FieldNotification::GFlow_LoadingViewModelDescriptor;
}

UE::FieldNotification::FFieldId UFlow_LoadingViewModel::GetFieldId(EField Field)
{
	return UE::FieldNotification::FFlow_LoadingViewModelDescriptor::MakeId(Field);
}

void UFlow_LoadingViewModel::BroadcastField(EField Field)
{
	BroadcastFieldValueChanged(GetFieldId(Field));
}

void UFlow_LoadingViewModel::SetLoadingState(float InProgress, const FText& InStatusLabel, bool bInVisible)
{
	// A negative progress means "indeterminate": store 0 for the bar but flip the indeterminate flag so
	// the UI shows a spinner instead.
	const bool bNewIndeterminate = (InProgress < 0.f);
	const float NewProgress = bNewIndeterminate ? 0.f : FMath::Clamp(InProgress, 0.f, 1.f);

	if (!FMath::IsNearlyEqual(Progress, NewProgress))
	{
		Progress = NewProgress;
		BroadcastField(EField::Progress);
	}

	if (bIndeterminate != bNewIndeterminate)
	{
		bIndeterminate = bNewIndeterminate;
		BroadcastField(EField::bIndeterminate);
	}

	if (!StatusLabel.EqualTo(InStatusLabel))
	{
		StatusLabel = InStatusLabel;
		BroadcastField(EField::StatusLabel);
	}

	SetVisible(bInVisible);
}

void UFlow_LoadingViewModel::SetVisible(bool bInVisible)
{
	if (bVisible != bInVisible)
	{
		bVisible = bInVisible;
		BroadcastField(EField::bVisible);
	}
}
