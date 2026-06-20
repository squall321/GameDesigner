// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "HUD_HudLayoutDataAsset.generated.h"

class UUserWidget;

/**
 * One slot definition in a HUD layout: a stable slot identity tag mapped to the (soft) widget
 * class that realises it, plus the layer and z-order it is placed on.
 *
 * Everything here is data-driven — there are NO hardcoded widget references in C++. The widget
 * class is a TSoftClassPtr so a layout can be authored/shipped without hard-loading every HUD
 * widget up front; the layout subsystem streams it on demand.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSHUD_API FHUD_LayoutSlot
{
	GENERATED_BODY()

	/**
	 * Stable identity of this slot (e.g. DP.HUD.Slot.HealthBar). Used as the map key the layout
	 * subsystem shows/hides by. Must be unique within a layout asset.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Layout",
		meta = (Categories = "DP.HUD"))
	FGameplayTag SlotTag;

	/** The widget class that realises this slot. Soft so it loads on demand. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Layout")
	TSoftClassPtr<UUserWidget> WidgetClass;

	/**
	 * The viewport layer this slot's widget is added to (e.g. DP.UI.Layer.HUD). When unset the
	 * layout subsystem falls back to the layout's DefaultLayer.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Layout",
		meta = (Categories = "DP.UI.Layer"))
	FGameplayTag LayerTag;

	/** ZOrder used when adding this slot's widget to the viewport. Higher draws on top. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Layout")
	int32 ZOrder = 0;

	/** When true the slot is created and shown as soon as the layout is applied; else it starts hidden. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Layout")
	bool bVisibleByDefault = true;

	/** True if this slot carries a valid identity and a non-empty widget class. */
	bool IsValidSlot() const
	{
		return SlotTag.IsValid() && !WidgetClass.IsNull();
	}
};

/**
 * Data asset describing a complete HUD layout: a set of named slots, each mapping a slot tag to a
 * soft widget class on a layer with a z-order.
 *
 * Resolved by DataTag through the core data registry (UDP_DataRegistrySubsystem::Find), so the
 * active layout can be swapped at runtime by tag without any hardcoded asset paths. The layout
 * subsystem (UHUD_HudLayoutSubsystem) consumes this to build/position the HUD widget layers.
 */
UCLASS(BlueprintType, meta = (DisplayName = "HUD Layout"))
class DESIGNPATTERNSHUD_API UHUD_HudLayoutDataAsset : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	UHUD_HudLayoutDataAsset();

	/** The slots that make up this layout, keyed (in the subsystem) by SlotTag. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Layout",
		meta = (TitleProperty = "SlotTag"))
	TArray<FHUD_LayoutSlot> Slots;

	/**
	 * Layer used for any slot that leaves its LayerTag unset. Defaults (in the constructor) to the
	 * HUD layer; documented defensive fallback so an asset with empty slot layers still places
	 * widgets sensibly.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD|Layout",
		meta = (Categories = "DP.UI.Layer"))
	FGameplayTag DefaultLayer;

	/**
	 * Find a slot definition by its tag.
	 * @return Pointer into the Slots array, or null if the slot is not defined in this layout.
	 */
	const FHUD_LayoutSlot* FindSlot(const FGameplayTag& SlotTag) const;

	/** Blueprint-friendly slot lookup; bFound communicates success without exposing a raw pointer. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "HUD|Layout")
	bool GetSlot(FGameplayTag SlotTag, FHUD_LayoutSlot& OutSlot) const;

	/** All slot tags defined in this layout, in authoring order. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "HUD|Layout")
	void GetSlotTags(TArray<FGameplayTag>& OutTags) const;

	//~ Begin UDP_DataAsset
	/** Collapses all HUD layout assets into one asset-manager type bucket ("HUD_Layout"). */
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset

#if WITH_EDITOR
	//~ Begin UObject (editor validation)
	/** Flags duplicate slot tags and slots missing a widget class / identity. */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
	//~ End UObject
#endif
};
