// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "Inventory/RPG_InventoryComponent.h"   // FRPG_ItemStack
#include "RPG_CraftCostTable.generated.h"

/**
 * Material cost for one upgrade/reroll step, keyed by rarity and the level being upgraded FROM. Optionally
 * also requires a currency amount (validated against ISeam_Wallet read-only; the actual debit stays on the
 * guarded wallet component, not here).
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSRPG_API FRPG_CraftCostRow
{
	GENERATED_BODY()

	/** Rarity this cost applies to (e.g. "RPG.Rarity.Rare"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Craft")
	FGameplayTag RarityTag;

	/** Upgrade level this row prices (the cost to go FROM this level to the next). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Craft", meta = (ClampMin = "0"))
	int32 FromLevel = 0;

	/** Material item stacks consumed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Craft")
	TArray<FRPG_ItemStack> Materials;

	/** Optional currency tag required (empty = no currency cost). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Craft")
	FGameplayTag CurrencyTag;

	/** Currency amount required when CurrencyTag is set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Craft", meta = (ClampMin = "0"))
	int64 CurrencyCost = 0;

	FRPG_CraftCostRow() = default;
};

/**
 * Salvage yield: the materials returned when salvaging an item of a given rarity.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSRPG_API FRPG_SalvageYield
{
	GENERATED_BODY()

	/** Rarity this yield applies to. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Craft")
	FGameplayTag RarityTag;

	/** Materials granted on salvage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Craft")
	TArray<FRPG_ItemStack> Materials;

	FRPG_SalvageYield() = default;
};

/**
 * Data-driven economics for item upgrade / reroll / salvage. Identity = inherited DataTag. No magic numbers:
 * every cost and yield is authored on the asset.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSRPG_API URPG_CraftCostTable : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/** Upgrade cost rows (matched by rarity + from-level). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Craft")
	TArray<FRPG_CraftCostRow> UpgradeCosts;

	/** Reroll cost rows (matched by rarity; FromLevel ignored). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Craft")
	TArray<FRPG_CraftCostRow> RerollCosts;

	/** Salvage yields by rarity. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Craft")
	TArray<FRPG_SalvageYield> SalvageYields;

	/** Resolve the upgrade cost row for (RarityTag, FromLevel). Returns false if none is authored. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Craft")
	bool GetUpgradeCost(FGameplayTag RarityTag, int32 FromLevel, FRPG_CraftCostRow& OutRow) const;

	/** Resolve the reroll cost row for RarityTag. Returns false if none is authored. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Craft")
	bool GetRerollCost(FGameplayTag RarityTag, FRPG_CraftCostRow& OutRow) const;

	/** Resolve salvage materials for RarityTag into OutMaterials. Returns false if none is authored. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Craft")
	bool GetSalvageYield(FGameplayTag RarityTag, TArray<FRPG_ItemStack>& OutMaterials) const;

	//~ Begin UDP_DataAsset
	/** Collapse all craft-cost tables into one shared "RPG_CraftCostTable" asset-manager bucket. */
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset
};
