// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "Item/RPG_ItemInstance.h"
#include "RPG_ItemRoller.generated.h"

class URPG_RarityTable;
class URPG_AffixDefinition;

/**
 * Deterministic, seeded item roller.
 *
 * Given an item tag, item level, an explicit integer seed and an optional forced rarity, RollItem produces a
 * fully-rolled FRPG_ItemInstance: it draws a rarity from RarityTable (or honours ForcedRarity), filters the
 * authored AffixPool by the item's ItemTypeTag and the rolled rarity, then spends the rarity's affix budget
 * on a weighted, budget-bounded affix draw, and finally fills the rarity's socket count with open sockets.
 *
 * Everything uses a single FRandomStream(Seed) — no global RNG — so the SAME seed reproduces the SAME item
 * on the server, on a client preview and during a reroll. The candidate affix pool is authored explicitly
 * (rather than scanned from the registry) so a roller's output is stable and reviewable.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSRPG_API URPG_ItemRoller : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/** Rarity table driving rarity draw, affix budget, affix count band and socket count. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Roller")
	TObjectPtr<URPG_RarityTable> RarityTable;

	/** The full pool of affixes this roller may draw from (filtered per item-type / rarity at roll time). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Roller")
	TArray<TObjectPtr<URPG_AffixDefinition>> AffixPool;

	/** Base durability granted to a freshly rolled item (used to seed Current/Max durability). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Roller", meta = (ClampMin = "0.0"))
	float BaseDurability = 100.f;

	/** Default socket type assigned to rolled-open sockets when the item type does not specify one. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Roller")
	FGameplayTag DefaultSocketType;

	/**
	 * Roll a complete instance.
	 * @param ItemTag       definition identity to stamp onto the instance.
	 * @param ItemLevel     scales affix magnitudes.
	 * @param Seed          deterministic seed for the whole roll.
	 * @param ForcedRarity  if valid, skips the rarity draw and uses this tier directly.
	 * @note InstanceId is left 0; the owning URPG_ItemInstanceComponent assigns the stable id on add.
	 */
	UFUNCTION(BlueprintCallable, Category = "RPG|Roller")
	FRPG_ItemInstance RollItem(FGameplayTag ItemTag, int32 ItemLevel, int32 Seed, FGameplayTag ForcedRarity) const;

	/** Gather every affix in the pool that may roll on ItemTypeTag at RarityTag. */
	void ResolveCandidateAffixes(FGameplayTag ItemTypeTag, FGameplayTag RarityTag,
		TArray<const URPG_AffixDefinition*>& Out) const;

	//~ Begin UDP_DataAsset
	/** Collapse all rollers into one shared "RPG_ItemRoller" asset-manager bucket. */
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset

private:
	/** Resolve an item definition's ItemTypeTag from the registry (empty if unknown). */
	FGameplayTag ResolveItemTypeTag(const FGameplayTag& ItemTag) const;
};
