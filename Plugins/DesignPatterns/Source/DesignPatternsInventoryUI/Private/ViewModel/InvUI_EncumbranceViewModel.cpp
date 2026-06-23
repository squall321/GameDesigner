// Copyright DesignPatterns plugin. All Rights Reserved.

#include "ViewModel/InvUI_EncumbranceViewModel.h"
#include "Seam/InvUI_SpatialFootprintProvider.h"
#include "Core/DPLog.h"

namespace UE::FieldNotification
{
	/** Hand-rolled descriptor enumerating UInvUI_EncumbranceViewModel's observable fields. */
	struct FInvUI_EncumbranceViewModelDescriptor : public IClassDescriptor
	{
		static const FName FieldNames[(int32)UInvUI_EncumbranceViewModel::EField::Num];

		static FFieldId MakeId(UInvUI_EncumbranceViewModel::EField Field)
		{
			const int32 Index = (int32)Field;
			return FFieldId(FieldNames[Index], Index);
		}

		virtual void ForEachField(const UClass* /*Class*/, TFunctionRef<bool(FFieldId)> Callback) const override
		{
			for (int32 Index = 0; Index < (int32)UInvUI_EncumbranceViewModel::EField::Num; ++Index)
			{
				if (!Callback(FFieldId(FieldNames[Index], Index)))
				{
					break;
				}
			}
		}
	};

	const FName FInvUI_EncumbranceViewModelDescriptor::FieldNames[(int32)UInvUI_EncumbranceViewModel::EField::Num] =
	{
		FName(TEXT("CurrentWeight")),
		FName(TEXT("MaxWeight")),
		FName(TEXT("CurrentVolume")),
		FName(TEXT("MaxVolume")),
		FName(TEXT("ItemCount")),
		FName(TEXT("bOverweight")),
	};

	static const FInvUI_EncumbranceViewModelDescriptor GInvUI_EncumbranceViewModelDescriptor;
}

UInvUI_EncumbranceViewModel::UInvUI_EncumbranceViewModel()
{
}

void UInvUI_EncumbranceViewModel::BeginDestroy()
{
	UnbindContainer();
	Super::BeginDestroy();
}

const UE::FieldNotification::IClassDescriptor& UInvUI_EncumbranceViewModel::GetFieldNotificationDescriptor() const
{
	return UE::FieldNotification::GInvUI_EncumbranceViewModelDescriptor;
}

UE::FieldNotification::FFieldId UInvUI_EncumbranceViewModel::GetFieldId(EField Field)
{
	return UE::FieldNotification::FInvUI_EncumbranceViewModelDescriptor::MakeId(Field);
}

void UInvUI_EncumbranceViewModel::BroadcastField(EField Field)
{
	BroadcastFieldValueChanged(GetFieldId(Field));
}

void UInvUI_EncumbranceViewModel::BindContainer(const TScriptInterface<IInvUI_ItemContainer>& Container)
{
	UnbindContainer();

	BoundContainer = Container;

	if (BoundContainer.GetObject() != nullptr)
	{
		// Subscribe to the seam dynamic delegate so the totals track backend changes.
		FInvUI_OnContainerChangedDynamic& Delegate =
			BoundContainer->GetOnContainerChangedDelegate();
		Delegate.AddDynamic(this, &UInvUI_EncumbranceViewModel::HandleContainerChanged);
		bSubscribed = true;
	}

	Recompute();
}

void UInvUI_EncumbranceViewModel::UnbindContainer()
{
	if (bSubscribed && BoundContainer.GetObject() != nullptr)
	{
		FInvUI_OnContainerChangedDynamic& Delegate =
			BoundContainer->GetOnContainerChangedDelegate();
		Delegate.RemoveDynamic(this, &UInvUI_EncumbranceViewModel::HandleContainerChanged);
	}
	bSubscribed = false;
	BoundContainer = TScriptInterface<IInvUI_ItemContainer>();

	// Zero the totals.
	SetProperty(GetFieldId(EField::CurrentWeight), CurrentWeight, 0.f);
	SetProperty(GetFieldId(EField::CurrentVolume), CurrentVolume, 0.f);
	SetProperty(GetFieldId(EField::ItemCount), ItemCount, 0);
	UpdateOverweight();
}

void UInvUI_EncumbranceViewModel::Recompute()
{
	float NewWeight = 0.f;
	float NewVolume = 0.f;
	int32 NewCount = 0;

	if (BoundContainer.GetObject() != nullptr)
	{
		TArray<FInvUI_SlotState> Slots;
		IInvUI_ItemContainer::Execute_GetSlots(BoundContainer.GetObject(), Slots);

		const bool bHasProvider =
			BoundContainer.GetObject()->GetClass()->ImplementsInterface(UInvUI_SpatialFootprintProvider::StaticClass());

		for (const FInvUI_SlotState& Slot : Slots)
		{
			if (!Slot.IsOccupied())
			{
				continue;
			}
			++NewCount;

			FInvUI_SpatialFootprint Footprint;
			bool bGotFootprint = false;

			// Prefer the in-payload footprint.
			if (Slot.ItemPayload.GetScriptStruct() == FInvUI_SpatialFootprint::StaticStruct())
			{
				Footprint = Slot.ItemPayload.Get<FInvUI_SpatialFootprint>();
				bGotFootprint = true;
			}
			else if (bHasProvider)
			{
				bGotFootprint = IInvUI_SpatialFootprintProvider::Execute_GetSlotFootprint(
					BoundContainer.GetObject(), Slot.SlotTag, Footprint);
			}

			if (bGotFootprint)
			{
				// Per-unit weight/volume scale with the stack count (a stack of 5 weighs 5x).
				const float Units = (float)FMath::Max(1, Slot.Count);
				NewWeight += Footprint.Weight * Units;
				NewVolume += Footprint.Volume * Units;
			}
		}
	}

	SetProperty(GetFieldId(EField::CurrentWeight), CurrentWeight, NewWeight);
	SetProperty(GetFieldId(EField::CurrentVolume), CurrentVolume, NewVolume);
	SetProperty(GetFieldId(EField::ItemCount), ItemCount, NewCount);
	UpdateOverweight();
}

void UInvUI_EncumbranceViewModel::SetLimits(float InMaxWeight, float InMaxVolume)
{
	SetProperty(GetFieldId(EField::MaxWeight), MaxWeight, FMath::Max(0.f, InMaxWeight));
	SetProperty(GetFieldId(EField::MaxVolume), MaxVolume, FMath::Max(0.f, InMaxVolume));
	UpdateOverweight();
}

void UInvUI_EncumbranceViewModel::UpdateOverweight()
{
	const bool bNow = (MaxWeight > 0.f) && (CurrentWeight > MaxWeight);
	SetProperty(GetFieldId(EField::bOverweight), bOverweight, bNow);
}

float UInvUI_EncumbranceViewModel::GetWeightFraction() const
{
	if (MaxWeight <= 0.f)
	{
		return 0.f;
	}
	return FMath::Clamp(CurrentWeight / MaxWeight, 0.f, 1.f);
}

void UInvUI_EncumbranceViewModel::HandleContainerChanged(const FInvUI_ContainerInstanceId& /*InContainerId*/)
{
	Recompute();
}
