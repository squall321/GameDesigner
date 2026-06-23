// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Objective/HUD_ObjectiveTrackerViewModel.h"

namespace UE::FieldNotification
{
	/** Descriptor enumerating UHUD_ObjectiveTrackerViewModel's observable fields by name. */
	struct FHUD_ObjectiveTrackerViewModelDescriptor : public IClassDescriptor
	{
		static const FName FieldNames[(int32)UHUD_ObjectiveTrackerViewModel::EField::Num];

		static FFieldId MakeId(UHUD_ObjectiveTrackerViewModel::EField Field)
		{
			const int32 Index = (int32)Field;
			return FFieldId(FieldNames[Index], Index);
		}

		virtual void ForEachField(const UClass* /*Class*/, TFunctionRef<bool(FFieldId)> Callback) const override
		{
			for (int32 Index = 0; Index < (int32)UHUD_ObjectiveTrackerViewModel::EField::Num; ++Index)
			{
				if (!Callback(FFieldId(FieldNames[Index], Index)))
				{
					break;
				}
			}
		}
	};

	const FName FHUD_ObjectiveTrackerViewModelDescriptor::FieldNames[(int32)UHUD_ObjectiveTrackerViewModel::EField::Num] =
	{
		FName(TEXT("Objectives")),
		FName(TEXT("PinnedCount")),
	};

	static const FHUD_ObjectiveTrackerViewModelDescriptor GHUD_ObjectiveTrackerViewModelDescriptor;
}

const UE::FieldNotification::IClassDescriptor& UHUD_ObjectiveTrackerViewModel::GetFieldNotificationDescriptor() const
{
	return UE::FieldNotification::GHUD_ObjectiveTrackerViewModelDescriptor;
}

UE::FieldNotification::FFieldId UHUD_ObjectiveTrackerViewModel::GetFieldId(EField Field)
{
	return UE::FieldNotification::FHUD_ObjectiveTrackerViewModelDescriptor::MakeId(Field);
}

void UHUD_ObjectiveTrackerViewModel::BroadcastField(EField Field)
{
	BroadcastFieldValueChanged(GetFieldId(Field));
}

int32 UHUD_ObjectiveTrackerViewModel::GetPinnedCount() const
{
	int32 Count = 0;
	for (const FHUD_ObjectiveView& View : Objectives)
	{
		if (View.bPinned)
		{
			++Count;
		}
	}
	return Count;
}

void UHUD_ObjectiveTrackerViewModel::SetObjectives(const TArray<FHUD_ObjectiveView>& InObjectives)
{
	// Compare structurally so we only wake bound views when something meaningfully changed.
	bool bChanged = (InObjectives.Num() != Objectives.Num());
	if (!bChanged)
	{
		for (int32 i = 0; i < InObjectives.Num(); ++i)
		{
			const FHUD_ObjectiveView& A = InObjectives[i];
			const FHUD_ObjectiveView& B = Objectives[i];
			if (A.ObjectiveId != B.ObjectiveId
				|| A.StateTag != B.StateTag
				|| A.bPinned != B.bPinned
				|| !A.Title.IdenticalTo(B.Title)
				|| !FMath::IsNearlyEqual(A.ProgressFraction, B.ProgressFraction, 0.001f))
			{
				bChanged = true;
				break;
			}
		}
	}

	if (bChanged)
	{
		const int32 OldPinned = GetPinnedCount();
		Objectives = InObjectives;
		BroadcastField(EField::Objectives);
		if (GetPinnedCount() != OldPinned)
		{
			BroadcastField(EField::PinnedCount);
		}
	}
}
