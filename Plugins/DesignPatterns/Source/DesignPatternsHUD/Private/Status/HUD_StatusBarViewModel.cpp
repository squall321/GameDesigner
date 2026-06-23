// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Status/HUD_StatusBarViewModel.h"
#include "Status/HUD_StatusBarStyleDataAsset.h"

namespace UE::FieldNotification
{
	/** Descriptor enumerating UHUD_StatusBarViewModel's observable fields by name. */
	struct FHUD_StatusBarViewModelDescriptor : public IClassDescriptor
	{
		static const FName FieldNames[(int32)UHUD_StatusBarViewModel::EField::Num];

		static FFieldId MakeId(UHUD_StatusBarViewModel::EField Field)
		{
			const int32 Index = (int32)Field;
			return FFieldId(FieldNames[Index], Index);
		}

		virtual void ForEachField(const UClass* /*Class*/, TFunctionRef<bool(FFieldId)> Callback) const override
		{
			for (int32 Index = 0; Index < (int32)UHUD_StatusBarViewModel::EField::Num; ++Index)
			{
				if (!Callback(FFieldId(FieldNames[Index], Index)))
				{
					break;
				}
			}
		}
	};

	const FName FHUD_StatusBarViewModelDescriptor::FieldNames[(int32)UHUD_StatusBarViewModel::EField::Num] =
	{
		FName(TEXT("Statuses")),
		FName(TEXT("ActiveCount")),
	};

	static const FHUD_StatusBarViewModelDescriptor GHUD_StatusBarViewModelDescriptor;
}

const UE::FieldNotification::IClassDescriptor& UHUD_StatusBarViewModel::GetFieldNotificationDescriptor() const
{
	return UE::FieldNotification::GHUD_StatusBarViewModelDescriptor;
}

UE::FieldNotification::FFieldId UHUD_StatusBarViewModel::GetFieldId(EField Field)
{
	return UE::FieldNotification::FHUD_StatusBarViewModelDescriptor::MakeId(Field);
}

void UHUD_StatusBarViewModel::BroadcastField(EField Field)
{
	BroadcastFieldValueChanged(GetFieldId(Field));
}

void UHUD_StatusBarViewModel::SetStatusSource(const TScriptInterface<ISeam_StatusProvider>& InSource)
{
	if (UObject* Obj = InSource.GetObject())
	{
		StatusSource = TWeakInterfacePtr<ISeam_StatusProvider>(*Obj);
	}
	else
	{
		StatusSource.Reset();
	}
	Refresh();
}

void UHUD_StatusBarViewModel::SetStyleAsset(UHUD_StatusBarStyleDataAsset* InStyle)
{
	Style = InStyle;
	Refresh();
}

void UHUD_StatusBarViewModel::Refresh()
{
	// Resolve the weak source; a destroyed/unset provider yields an empty bar.
	UObject* SourceObj = StatusSource.GetObject();
	TArray<FSeam_StatusEntry> Raw;
	if (SourceObj && SourceObj->GetClass()->ImplementsInterface(USeam_StatusProvider::StaticClass()))
	{
		ISeam_StatusProvider::Execute_GetActiveStatuses(SourceObj, Raw);
	}

	TArray<FHUD_StatusEntryView> NewStatuses;
	NewStatuses.Reserve(Raw.Num());
	for (const FSeam_StatusEntry& Entry : Raw)
	{
		if (!Entry.IsValidEntry())
		{
			continue;
		}

		FHUD_StatusEntryView View;
		View.StatusTag = Entry.StatusTag;
		View.CategoryTag = Entry.CategoryTag;
		View.Stacks = FMath::Max(1, Entry.Stacks);
		View.RemainingSeconds = Entry.RemainingSeconds;
		View.RemainingFraction = Entry.GetRemainingFraction();
		View.Magnitude = Entry.Magnitude;

		if (Style)
		{
			// Prefer a status-tag-specific row; fall back to the category tag's row if present.
			if (!Style->ResolveStyle(Entry.StatusTag, View.Icon, View.Tint))
			{
				Style->ResolveStyle(Entry.CategoryTag, View.Icon, View.Tint);
			}
		}

		NewStatuses.Add(MoveTemp(View));
	}

	// Only broadcast when the projected set actually changed (durations tick continuously but we compare on
	// a coarse tolerance via the seam entry compare; here we compare the projected views structurally).
	bool bChanged = (NewStatuses.Num() != Statuses.Num());
	if (!bChanged)
	{
		for (int32 i = 0; i < NewStatuses.Num(); ++i)
		{
			const FHUD_StatusEntryView& A = NewStatuses[i];
			const FHUD_StatusEntryView& B = Statuses[i];
			if (A.StatusTag != B.StatusTag
				|| A.Stacks != B.Stacks
				|| !FMath::IsNearlyEqual(A.RemainingFraction, B.RemainingFraction, 0.01f)
				|| !FMath::IsNearlyEqual(A.Magnitude, B.Magnitude, KINDA_SMALL_NUMBER))
			{
				bChanged = true;
				break;
			}
		}
	}

	if (bChanged)
	{
		const int32 OldCount = Statuses.Num();
		Statuses = MoveTemp(NewStatuses);
		BroadcastField(EField::Statuses);
		if (Statuses.Num() != OldCount)
		{
			BroadcastField(EField::ActiveCount);
		}
	}
}
