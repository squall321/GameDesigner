// Copyright DesignPatterns plugin. All Rights Reserved.

#include "ViewModel/InvUI_SlotViewModel.h"
#include "Core/DPLog.h"

namespace UE::FieldNotification
{
	/**
	 * Hand-rolled descriptor enumerating UInvUI_SlotViewModel's observable fields. Chains nothing
	 * extra (the base declares no fields) and reports one FFieldId per EField value, named to match
	 * the getter-less field name so K2_BroadcastFieldValueChanged can resolve them by name.
	 */
	struct FInvUI_SlotViewModelDescriptor : public IClassDescriptor
	{
		static const FName FieldNames[(int32)UInvUI_SlotViewModel::EField::Num];

		static FFieldId MakeId(UInvUI_SlotViewModel::EField Field)
		{
			const int32 Index = (int32)Field;
			return FFieldId(FieldNames[Index], Index);
		}

		virtual void ForEachField(const UClass* /*Class*/, TFunctionRef<bool(FFieldId)> Callback) const override
		{
			for (int32 Index = 0; Index < (int32)UInvUI_SlotViewModel::EField::Num; ++Index)
			{
				if (!Callback(FFieldId(FieldNames[Index], Index)))
				{
					break;
				}
			}
		}
	};

	const FName FInvUI_SlotViewModelDescriptor::FieldNames[(int32)UInvUI_SlotViewModel::EField::Num] =
	{
		FName(TEXT("SlotTag")),
		FName(TEXT("ItemTag")),
		FName(TEXT("Count")),
		FName(TEXT("Empty")),
		FName(TEXT("DisplayName")),
		FName(TEXT("Description")),
		FName(TEXT("Icon")),
		FName(TEXT("QualityColor")),
		FName(TEXT("Column")),
		FName(TEXT("Row")),
		FName(TEXT("DropTargetHighlight")),
	};

	static const FInvUI_SlotViewModelDescriptor GInvUI_SlotViewModelDescriptor;
}

UInvUI_SlotViewModel::UInvUI_SlotViewModel()
{
}

const UE::FieldNotification::IClassDescriptor& UInvUI_SlotViewModel::GetFieldNotificationDescriptor() const
{
	return UE::FieldNotification::GInvUI_SlotViewModelDescriptor;
}

UE::FieldNotification::FFieldId UInvUI_SlotViewModel::GetFieldId(EField Field)
{
	return UE::FieldNotification::FInvUI_SlotViewModelDescriptor::MakeId(Field);
}

void UInvUI_SlotViewModel::BroadcastField(EField Field)
{
	BroadcastFieldValueChanged(GetFieldId(Field));
}

bool UInvUI_SlotViewModel::SetSlotState(const FInvUI_SlotState& InState)
{
	bool bChanged = false;

	if (SlotTag != InState.SlotTag)
	{
		SlotTag = InState.SlotTag;
		BroadcastField(EField::SlotTag);
		bChanged = true;
	}
	if (ItemTag != InState.ItemTag)
	{
		ItemTag = InState.ItemTag;
		BroadcastField(EField::ItemTag);
		bChanged = true;
	}
	if (Count != InState.Count)
	{
		Count = InState.Count;
		BroadcastField(EField::Count);
		bChanged = true;
	}

	const bool bNowEmpty = InState.IsEmpty();
	if (bEmpty != bNowEmpty)
	{
		bEmpty = bNowEmpty;
		BroadcastField(EField::Empty);
		bChanged = true;
	}

	// Becoming empty clears any stale display info so a freed slot doesn't keep a ghost icon.
	if (bEmpty && (Icon != nullptr || !DisplayName.IsEmpty()))
	{
		ClearDisplayInfo();
		bChanged = true;
	}

	return bChanged;
}

bool UInvUI_SlotViewModel::ApplyDisplayInfo(const FText& InName, const FText& InDescription,
	UTexture2D* InIcon, const FLinearColor& InQualityColor)
{
	bool bChanged = false;

	if (!DisplayName.EqualTo(InName))
	{
		DisplayName = InName;
		BroadcastField(EField::DisplayName);
		bChanged = true;
	}
	if (!Description.EqualTo(InDescription))
	{
		Description = InDescription;
		BroadcastField(EField::Description);
		bChanged = true;
	}
	if (Icon != InIcon)
	{
		Icon = InIcon;
		BroadcastField(EField::Icon);
		bChanged = true;
	}
	if (QualityColor != InQualityColor)
	{
		QualityColor = InQualityColor;
		BroadcastField(EField::QualityColor);
		bChanged = true;
	}
	return bChanged;
}

void UInvUI_SlotViewModel::ClearDisplayInfo()
{
	if (!DisplayName.IsEmpty())
	{
		DisplayName = FText::GetEmpty();
		BroadcastField(EField::DisplayName);
	}
	if (!Description.IsEmpty())
	{
		Description = FText::GetEmpty();
		BroadcastField(EField::Description);
	}
	if (Icon != nullptr)
	{
		Icon = nullptr;
		BroadcastField(EField::Icon);
	}
	if (QualityColor != FLinearColor::White)
	{
		QualityColor = FLinearColor::White;
		BroadcastField(EField::QualityColor);
	}
}

void UInvUI_SlotViewModel::SetCellPosition(int32 InColumn, int32 InRow)
{
	if (Column != InColumn)
	{
		Column = InColumn;
		BroadcastField(EField::Column);
	}
	if (Row != InRow)
	{
		Row = InRow;
		BroadcastField(EField::Row);
	}
}

void UInvUI_SlotViewModel::SetDropTargetHighlight(bool bInHighlighted)
{
	if (bDropTargetHighlight != bInHighlighted)
	{
		bDropTargetHighlight = bInHighlighted;
		BroadcastField(EField::DropTargetHighlight);
	}
}
