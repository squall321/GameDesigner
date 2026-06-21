// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "RPG_EquipSlotLayout.generated.h"

/**
 * One typed equipment slot: a slot tag, the item-type tags it accepts and an optional cap on how many
 * sockets a placed item may use in this slot. No hardcoded slot list — projects author all slots.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSRPG_API FRPG_EquipSlotDef
{
	GENERATED_BODY()

	/** Slot identity (e.g. "RPG.Slot.Weapon"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Slots")
	FGameplayTag SlotTag;

	/** Item-type tags this slot accepts (matched against URPG_ItemDefinition::ItemTypeTag). Empty = any. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Slots")
	FGameplayTagContainer AcceptedItemTypes;

	/** Maximum sockets usable while equipped here (0 = no limit beyond what the item itself provides). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Slots", meta = (ClampMin = "0"))
	int32 MaxUsableSockets = 0;

	FRPG_EquipSlotDef() = default;
};

/**
 * Data-driven typed-slot layout for an equip-slots component. Identity = inherited DataTag.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSRPG_API URPG_EquipSlotLayout : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/** The authored slots. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Slots")
	TArray<FRPG_EquipSlotDef> Slots;

	/** Find the slot definition for SlotTag, or nullptr. */
	const FRPG_EquipSlotDef* FindSlot(const FGameplayTag& SlotTag) const;

	/** True if SlotTag exists and accepts ItemTypeTag. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Slots")
	bool AcceptsType(FGameplayTag SlotTag, FGameplayTag ItemTypeTag) const;

	/** True if SlotTag is defined in this layout. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Slots")
	bool HasSlot(FGameplayTag SlotTag) const { return FindSlot(SlotTag) != nullptr; }

	//~ Begin UDP_DataAsset
	/** Collapse all slot layouts into one shared "RPG_EquipSlotLayout" asset-manager bucket. */
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset
};
