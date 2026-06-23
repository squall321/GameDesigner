// Copyright DesignPatterns plugin. All Rights Reserved.

#include "ScreenState/HUD_ScreenStateViewModel.h"

namespace UE::FieldNotification
{
	/** Descriptor enumerating UHUD_ScreenStateViewModel's observable fields by name. */
	struct FHUD_ScreenStateViewModelDescriptor : public IClassDescriptor
	{
		static const FName FieldNames[(int32)UHUD_ScreenStateViewModel::EField::Num];

		static FFieldId MakeId(UHUD_ScreenStateViewModel::EField Field)
		{
			const int32 Index = (int32)Field;
			return FFieldId(FieldNames[Index], Index);
		}

		virtual void ForEachField(const UClass* /*Class*/, TFunctionRef<bool(FFieldId)> Callback) const override
		{
			for (int32 Index = 0; Index < (int32)UHUD_ScreenStateViewModel::EField::Num; ++Index)
			{
				if (!Callback(FFieldId(FieldNames[Index], Index)))
				{
					break;
				}
			}
		}
	};

	const FName FHUD_ScreenStateViewModelDescriptor::FieldNames[(int32)UHUD_ScreenStateViewModel::EField::Num] =
	{
		FName(TEXT("VignetteIntensity")),
		FName(TEXT("HitDirections")),
		FName(TEXT("DamageFlashAlpha")),
	};

	static const FHUD_ScreenStateViewModelDescriptor GHUD_ScreenStateViewModelDescriptor;
}

const UE::FieldNotification::IClassDescriptor& UHUD_ScreenStateViewModel::GetFieldNotificationDescriptor() const
{
	return UE::FieldNotification::GHUD_ScreenStateViewModelDescriptor;
}

UE::FieldNotification::FFieldId UHUD_ScreenStateViewModel::GetFieldId(EField Field)
{
	return UE::FieldNotification::FHUD_ScreenStateViewModelDescriptor::MakeId(Field);
}

void UHUD_ScreenStateViewModel::BroadcastField(EField Field)
{
	BroadcastFieldValueChanged(GetFieldId(Field));
}

void UHUD_ScreenStateViewModel::SetVignetteIntensity(float Intensity)
{
	const float Clamped = FMath::Clamp(Intensity, 0.f, 1.f);
	SetProperty(GetFieldId(EField::VignetteIntensity), VignetteIntensity, Clamped);
}

void UHUD_ScreenStateViewModel::SetDamageFlashAlpha(float Alpha)
{
	const float Clamped = FMath::Clamp(Alpha, 0.f, 1.f);
	SetProperty(GetFieldId(EField::DamageFlashAlpha), DamageFlashAlpha, Clamped);
}

void UHUD_ScreenStateViewModel::SetHitDirections(const TArray<FHUD_HitDirectionView>& InDirections)
{
	// Alphas tick continuously while indicators are active, so store + broadcast unconditionally; when the
	// set is empty and was already empty, skip to avoid waking bound views every frame.
	if (HitDirections.Num() == 0 && InDirections.Num() == 0)
	{
		return;
	}
	HitDirections = InDirections;
	BroadcastField(EField::HitDirections);
}
