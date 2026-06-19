// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "Engine/Texture2D.h"
#include "RPG_ItemDefinition.generated.h"

/**
 * Tag-identified definition of a single RPG item type.
 *
 * Reuses the core data asset (UDP_DataAsset) so item identity is the inherited
 * DataTag (e.g. "RPG.Item.Sword.Iron") and the asset is resolvable, load-free, through
 * the core data registry (UDP_DataRegistrySubsystem::FindByTag). Inventories store only
 * the item's DataTag plus a count, never a hard pointer to the definition, so the catalog
 * can grow/reorder without touching saved inventories.
 *
 * Add item families by authoring child data assets; group them with ItemTypeTag.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSRPG_API URPG_ItemDefinition : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	URPG_ItemDefinition();

	/** Player-facing item name shown in inventory UI. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Item")
	FText ItemDisplayName;

	/** Soft icon reference; left unloaded until UI needs to draw it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Item")
	TSoftObjectPtr<UTexture2D> Icon;

	/**
	 * Maximum number of this item that may occupy one inventory slot/stack.
	 * Clamped to >= 1; non-stackable items use 1.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Item", meta = (ClampMin = "1"))
	int32 MaxStackSize = 1;

	/**
	 * Classification tag (e.g. "RPG.ItemType.Weapon", "RPG.ItemType.Consumable").
	 * Used by equipment slots and gameplay filters; distinct from the identity DataTag.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Item")
	FGameplayTag ItemTypeTag;

	/** Per-unit weight (encumbrance units). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Item", meta = (ClampMin = "0.0"))
	float Weight = 0.f;

	/** Per-unit monetary value (vendor/base price). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Item", meta = (ClampMin = "0"))
	int32 Value = 0;

	/** True when MaxStackSize allows more than one unit per stack. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Item")
	bool IsStackable() const { return MaxStackSize > 1; }

	//~ Begin UDP_DataAsset
	/** Collapse every item definition into one shared "RPG_Item" asset-manager bucket. */
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset
};
