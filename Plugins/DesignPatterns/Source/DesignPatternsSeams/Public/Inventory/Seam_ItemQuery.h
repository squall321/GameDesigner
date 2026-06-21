// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Seam_ItemQuery.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_ItemQuery : public UInterface
{
	GENERATED_BODY()
};

/**
 * Read-only inventory query seam, implemented additively by the RPG inventory component. Quest gates,
 * dialogue conditions, crafting affordability and HUD read item possession through it without depending
 * on the RPG module. Items are tag-keyed so any game defines its own item taxonomy.
 *
 * This seam is intentionally read-only: removing tradeable items is an authority-only mutation exposed by
 * the separate ISeam_TradableInventory, never as a generic query method.
 */
class DESIGNPATTERNSSEAMS_API ISeam_ItemQuery
{
	GENERATED_BODY()

public:
	/** Total count of ItemTag the inventory currently holds (0 if none). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Inventory")
	int32 GetItemCount(FGameplayTag ItemTag) const;

	/** True if the inventory holds at least Count of ItemTag. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Inventory")
	bool HasItem(FGameplayTag ItemTag, int32 Count) const;
};
