// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Reticle/HUD_ReticleViewModel.h"

namespace UE::FieldNotification
{
	/** Descriptor enumerating UHUD_ReticleViewModel's observable fields by name. */
	struct FHUD_ReticleViewModelDescriptor : public IClassDescriptor
	{
		static const FName FieldNames[(int32)UHUD_ReticleViewModel::EField::Num];

		static FFieldId MakeId(UHUD_ReticleViewModel::EField Field)
		{
			const int32 Index = (int32)Field;
			return FFieldId(FieldNames[Index], Index);
		}

		virtual void ForEachField(const UClass* /*Class*/, TFunctionRef<bool(FFieldId)> Callback) const override
		{
			for (int32 Index = 0; Index < (int32)UHUD_ReticleViewModel::EField::Num; ++Index)
			{
				if (!Callback(FFieldId(FieldNames[Index], Index)))
				{
					break;
				}
			}
		}
	};

	const FName FHUD_ReticleViewModelDescriptor::FieldNames[(int32)UHUD_ReticleViewModel::EField::Num] =
	{
		FName(TEXT("SpreadDegrees")),
		FName(TEXT("HitConfirmAlpha")),
		FName(TEXT("TargetTypeTag")),
		FName(TEXT("bVisible")),
	};

	static const FHUD_ReticleViewModelDescriptor GHUD_ReticleViewModelDescriptor;
}

const UE::FieldNotification::IClassDescriptor& UHUD_ReticleViewModel::GetFieldNotificationDescriptor() const
{
	return UE::FieldNotification::GHUD_ReticleViewModelDescriptor;
}

UE::FieldNotification::FFieldId UHUD_ReticleViewModel::GetFieldId(EField Field)
{
	return UE::FieldNotification::FHUD_ReticleViewModelDescriptor::MakeId(Field);
}

void UHUD_ReticleViewModel::SetSpread(float Degrees)
{
	SetProperty(GetFieldId(EField::SpreadDegrees), SpreadDegrees, Degrees);
}

void UHUD_ReticleViewModel::SetHitConfirmAlpha(float Alpha)
{
	const float Clamped = FMath::Clamp(Alpha, 0.f, 1.f);
	SetProperty(GetFieldId(EField::HitConfirmAlpha), HitConfirmAlpha, Clamped);
}

void UHUD_ReticleViewModel::SetTargetTypeTag(FGameplayTag Tag)
{
	SetProperty(GetFieldId(EField::TargetTypeTag), TargetTypeTag, Tag);
}

void UHUD_ReticleViewModel::SetVisible(bool bInVisible)
{
	SetProperty(GetFieldId(EField::bVisible), bVisible, bInVisible);
}
