// Copyright DesignPatterns plugin. All Rights Reserved.

#include "ViewModel/InvUI_QuantitySplitterViewModel.h"
#include "Mediator/InvUI_ContainerMediatorComponent.h"
#include "Core/DPLog.h"

namespace UE::FieldNotification
{
	/** Hand-rolled descriptor enumerating UInvUI_QuantitySplitterViewModel's observable fields. */
	struct FInvUI_QuantitySplitterViewModelDescriptor : public IClassDescriptor
	{
		static const FName FieldNames[(int32)UInvUI_QuantitySplitterViewModel::EField::Num];

		static FFieldId MakeId(UInvUI_QuantitySplitterViewModel::EField Field)
		{
			const int32 Index = (int32)Field;
			return FFieldId(FieldNames[Index], Index);
		}

		virtual void ForEachField(const UClass* /*Class*/, TFunctionRef<bool(FFieldId)> Callback) const override
		{
			for (int32 Index = 0; Index < (int32)UInvUI_QuantitySplitterViewModel::EField::Num; ++Index)
			{
				if (!Callback(FFieldId(FieldNames[Index], Index)))
				{
					break;
				}
			}
		}
	};

	const FName FInvUI_QuantitySplitterViewModelDescriptor::FieldNames[(int32)UInvUI_QuantitySplitterViewModel::EField::Num] =
	{
		FName(TEXT("Selected")),
		FName(TEXT("Min")),
		FName(TEXT("Max")),
		FName(TEXT("SlotTag")),
	};

	static const FInvUI_QuantitySplitterViewModelDescriptor GInvUI_QuantitySplitterViewModelDescriptor;
}

UInvUI_QuantitySplitterViewModel::UInvUI_QuantitySplitterViewModel()
{
}

const UE::FieldNotification::IClassDescriptor& UInvUI_QuantitySplitterViewModel::GetFieldNotificationDescriptor() const
{
	return UE::FieldNotification::GInvUI_QuantitySplitterViewModelDescriptor;
}

UE::FieldNotification::FFieldId UInvUI_QuantitySplitterViewModel::GetFieldId(EField Field)
{
	return UE::FieldNotification::FInvUI_QuantitySplitterViewModelDescriptor::MakeId(Field);
}

void UInvUI_QuantitySplitterViewModel::BroadcastField(EField Field)
{
	BroadcastFieldValueChanged(GetFieldId(Field));
}

void UInvUI_QuantitySplitterViewModel::Begin(FInvUI_ContainerInstanceId InSourceContainer,
	FGameplayTag InSlotTag, int32 StackCount)
{
	SourceContainer = InSourceContainer;
	SetProperty(GetFieldId(EField::SlotTag), SlotTag, InSlotTag);

	const int32 NewMin = 1;
	// You cannot "split off" the whole stack — that is a plain move — so the max split is N-1.
	const int32 NewMax = FMath::Max(0, StackCount - 1);

	SetProperty(GetFieldId(EField::Min), Min, NewMin);
	SetProperty(GetFieldId(EField::Max), Max, NewMax);

	// Default the selection to half the stack, rounded up, clamped into range.
	const int32 NewSelected = FMath::Clamp(FMath::DivideAndRoundUp(StackCount, 2), NewMin, FMath::Max(NewMin, NewMax));
	SetProperty(GetFieldId(EField::Selected), Selected, NewSelected);
}

void UInvUI_QuantitySplitterViewModel::SetSelected(int32 InSelected)
{
	const int32 Clamped = FMath::Clamp(InSelected, Min, FMath::Max(Min, Max));
	SetProperty(GetFieldId(EField::Selected), Selected, Clamped);
}

void UInvUI_QuantitySplitterViewModel::StepSelected(int32 Delta)
{
	SetSelected(Selected + Delta);
}

void UInvUI_QuantitySplitterViewModel::ConfirmSplit(UInvUI_ContainerMediatorComponent* Mediator,
	FInvUI_ContainerInstanceId ToContainer)
{
	if (Mediator == nullptr)
	{
		UE_LOG(LogDP, Warning, TEXT("UInvUI_QuantitySplitterViewModel::ConfirmSplit: null mediator; ignored."));
		return;
	}
	if (!IsValidSession() || !SlotTag.IsValid() || !SourceContainer.IsValid())
	{
		UE_LOG(LogDP, Verbose, TEXT("UInvUI_QuantitySplitterViewModel::ConfirmSplit: invalid session; ignored."));
		return;
	}

	// A split is just a partial move with an empty destination slot (the backend places the split
	// stack). The server re-resolves both containers and re-validates the move authoritatively.
	Mediator->RequestMoveByIdentity(
		SourceContainer, SlotTag,
		ToContainer, FGameplayTag(),
		Selected);
}
