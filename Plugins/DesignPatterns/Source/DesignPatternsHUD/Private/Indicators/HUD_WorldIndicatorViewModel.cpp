// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Indicators/HUD_WorldIndicatorViewModel.h"

namespace UE::FieldNotification
{
	/** Descriptor enumerating UHUD_WorldIndicatorViewModel's observable fields by name. */
	struct FHUD_WorldIndicatorViewModelDescriptor : public IClassDescriptor
	{
		static const FName FieldNames[(int32)UHUD_WorldIndicatorViewModel::EField::Num];

		static FFieldId MakeId(UHUD_WorldIndicatorViewModel::EField Field)
		{
			const int32 Index = (int32)Field;
			return FFieldId(FieldNames[Index], Index);
		}

		virtual void ForEachField(const UClass* /*Class*/, TFunctionRef<bool(FFieldId)> Callback) const override
		{
			for (int32 Index = 0; Index < (int32)UHUD_WorldIndicatorViewModel::EField::Num; ++Index)
			{
				if (!Callback(FFieldId(FieldNames[Index], Index)))
				{
					break;
				}
			}
		}
	};

	const FName FHUD_WorldIndicatorViewModelDescriptor::FieldNames[(int32)UHUD_WorldIndicatorViewModel::EField::Num] =
	{
		FName(TEXT("Indicators")),
	};

	static const FHUD_WorldIndicatorViewModelDescriptor GHUD_WorldIndicatorViewModelDescriptor;
}

const UE::FieldNotification::IClassDescriptor& UHUD_WorldIndicatorViewModel::GetFieldNotificationDescriptor() const
{
	return UE::FieldNotification::GHUD_WorldIndicatorViewModelDescriptor;
}

UE::FieldNotification::FFieldId UHUD_WorldIndicatorViewModel::GetFieldId(EField Field)
{
	return UE::FieldNotification::FHUD_WorldIndicatorViewModelDescriptor::MakeId(Field);
}

void UHUD_WorldIndicatorViewModel::BroadcastField(EField Field)
{
	BroadcastFieldValueChanged(GetFieldId(Field));
}

void UHUD_WorldIndicatorViewModel::SetIconTable(const TArray<FHUD_MarkerIconRow>& InRows)
{
	IconTable = InRows;
}

TSoftObjectPtr<UTexture2D> UHUD_WorldIndicatorViewModel::ResolveIconForTag(const FGameplayTag& MarkerTag) const
{
	if (!MarkerTag.IsValid())
	{
		return TSoftObjectPtr<UTexture2D>();
	}
	// Exact match wins; else most-specific matching parent (longest tag string) — mirrors the minimap policy.
	const FHUD_MarkerIconRow* Best = nullptr;
	int32 BestDepth = -1;
	for (const FHUD_MarkerIconRow& Row : IconTable)
	{
		if (!Row.MarkerTag.IsValid())
		{
			continue;
		}
		if (Row.MarkerTag == MarkerTag)
		{
			return Row.Icon;
		}
		if (MarkerTag.MatchesTag(Row.MarkerTag))
		{
			const int32 Depth = Row.MarkerTag.ToString().Len();
			if (Depth > BestDepth)
			{
				BestDepth = Depth;
				Best = &Row;
			}
		}
	}
	return Best ? Best->Icon : TSoftObjectPtr<UTexture2D>();
}

void UHUD_WorldIndicatorViewModel::SetIndicators(const TArray<FHUD_WorldIndicatorView>& InIndicators)
{
	// The subsystem reprojects every refresh (positions/opacity change continuously), so store + broadcast.
	Indicators = InIndicators;
	BroadcastField(EField::Indicators);
}
