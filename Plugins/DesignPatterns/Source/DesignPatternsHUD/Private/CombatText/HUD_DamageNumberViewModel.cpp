// Copyright DesignPatterns plugin. All Rights Reserved.

#include "CombatText/HUD_DamageNumberViewModel.h"

namespace UE::FieldNotification
{
	/** Descriptor enumerating UHUD_DamageNumberViewModel's observable fields by name. */
	struct FHUD_DamageNumberViewModelDescriptor : public IClassDescriptor
	{
		static const FName FieldNames[(int32)UHUD_DamageNumberViewModel::EField::Num];

		static FFieldId MakeId(UHUD_DamageNumberViewModel::EField Field)
		{
			const int32 Index = (int32)Field;
			return FFieldId(FieldNames[Index], Index);
		}

		virtual void ForEachField(const UClass* /*Class*/, TFunctionRef<bool(FFieldId)> Callback) const override
		{
			for (int32 Index = 0; Index < (int32)UHUD_DamageNumberViewModel::EField::Num; ++Index)
			{
				if (!Callback(FFieldId(FieldNames[Index], Index)))
				{
					break;
				}
			}
		}
	};

	const FName FHUD_DamageNumberViewModelDescriptor::FieldNames[(int32)UHUD_DamageNumberViewModel::EField::Num] =
	{
		FName(TEXT("Numbers")),
	};

	static const FHUD_DamageNumberViewModelDescriptor GHUD_DamageNumberViewModelDescriptor;
}

const UE::FieldNotification::IClassDescriptor& UHUD_DamageNumberViewModel::GetFieldNotificationDescriptor() const
{
	return UE::FieldNotification::GHUD_DamageNumberViewModelDescriptor;
}

UE::FieldNotification::FFieldId UHUD_DamageNumberViewModel::GetFieldId(EField Field)
{
	return UE::FieldNotification::FHUD_DamageNumberViewModelDescriptor::MakeId(Field);
}

void UHUD_DamageNumberViewModel::BroadcastField(EField Field)
{
	BroadcastFieldValueChanged(GetFieldId(Field));
}

void UHUD_DamageNumberViewModel::SetNumbers(const TArray<FHUD_FloatingTextView>& InNumbers)
{
	// The subsystem owns the comparison/throttling (it only calls us when the live set advanced), so we
	// always store + broadcast here: per-item LifetimeAlpha changes every tick and the view must re-read.
	Numbers = InNumbers;
	BroadcastField(EField::Numbers);
}
