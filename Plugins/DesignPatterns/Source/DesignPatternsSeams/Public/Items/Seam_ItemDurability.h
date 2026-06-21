// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Seam_ItemDurability.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_ItemDurability : public UInterface
{
	GENERATED_BODY()
};

/**
 * Soft bridge to a durability backend (e.g. a survival durability component) so RPG crafting/repair and
 * equipment systems can read and modify item wear WITHOUT depending on the Survival module. Resolved as a
 * TScriptInterface<ISeam_ItemDurability>; the seam is OPTIONAL — when no provider is present, callers fall
 * back to their own per-instance durability field (e.g. FRPG_ItemInstance.CurrentDurability).
 *
 * Read methods are const and client-safe; ApplyWear/Repair are authority-only mutations (the implementer
 * guards authority and no-ops on clients).
 */
class DESIGNPATTERNSSEAMS_API ISeam_ItemDurability
{
	GENERATED_BODY()

public:
	/** Current durability of the item as a normalized [0,1] fraction (1 = pristine, 0 = broken). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Items")
	float GetDurabilityNormalized() const;

	/** True when the item has reached zero durability and is considered broken. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Items")
	bool IsBroken() const;

	/** Apply Amount of wear (durability loss, in item-native units). AUTHORITY ONLY. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Items")
	void ApplyWear(float Amount);

	/** Restore Amount of durability (in item-native units), clamped to the item's max. AUTHORITY ONLY. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Items")
	void Repair(float Amount);
};
