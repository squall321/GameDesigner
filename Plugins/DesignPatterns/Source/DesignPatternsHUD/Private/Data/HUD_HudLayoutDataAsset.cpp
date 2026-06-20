// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Data/HUD_HudLayoutDataAsset.h"
#include "HUD_HudNotifyTags.h"
#include "Core/DPLog.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

UHUD_HudLayoutDataAsset::UHUD_HudLayoutDataAsset()
{
	// Documented defensive default: an asset whose slots omit a layer still places widgets on the
	// HUD layer. Designers override DefaultLayer (and per-slot LayerTag) freely.
	DefaultLayer = HUDTags::UI_Layer_HUD;
}

const FHUD_LayoutSlot* UHUD_HudLayoutDataAsset::FindSlot(const FGameplayTag& SlotTag) const
{
	if (!SlotTag.IsValid())
	{
		return nullptr;
	}
	return Slots.FindByPredicate([&SlotTag](const FHUD_LayoutSlot& Slot)
	{
		return Slot.SlotTag == SlotTag;
	});
}

bool UHUD_HudLayoutDataAsset::GetSlot(FGameplayTag SlotTag, FHUD_LayoutSlot& OutSlot) const
{
	if (const FHUD_LayoutSlot* Found = FindSlot(SlotTag))
	{
		OutSlot = *Found;
		return true;
	}
	OutSlot = FHUD_LayoutSlot();
	return false;
}

void UHUD_HudLayoutDataAsset::GetSlotTags(TArray<FGameplayTag>& OutTags) const
{
	OutTags.Reset(Slots.Num());
	for (const FHUD_LayoutSlot& Slot : Slots)
	{
		OutTags.Add(Slot.SlotTag);
	}
}

FName UHUD_HudLayoutDataAsset::GetDataAssetType_Implementation() const
{
	// Collapse every HUD layout subclass into one asset-manager bucket so games can scan/preload
	// all layouts by a single primary-asset type.
	return FName(TEXT("HUD_Layout"));
}

#if WITH_EDITOR
EDataValidationResult UHUD_HudLayoutDataAsset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	TSet<FGameplayTag> SeenTags;
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		const FHUD_LayoutSlot& Slot = Slots[Index];

		if (!Slot.SlotTag.IsValid())
		{
			Context.AddError(FText::FromString(
				FString::Printf(TEXT("HUD layout slot [%d] has an invalid SlotTag."), Index)));
			Result = EDataValidationResult::Invalid;
			continue;
		}

		bool bAlreadySeen = false;
		SeenTags.Add(Slot.SlotTag, &bAlreadySeen);
		if (bAlreadySeen)
		{
			Context.AddError(FText::FromString(
				FString::Printf(TEXT("HUD layout slot [%d] duplicates SlotTag '%s'."),
					Index, *Slot.SlotTag.ToString())));
			Result = EDataValidationResult::Invalid;
		}

		if (Slot.WidgetClass.IsNull())
		{
			Context.AddError(FText::FromString(
				FString::Printf(TEXT("HUD layout slot '%s' has no WidgetClass."),
					*Slot.SlotTag.ToString())));
			Result = EDataValidationResult::Invalid;
		}
	}

	return Result;
}
#endif // WITH_EDITOR
