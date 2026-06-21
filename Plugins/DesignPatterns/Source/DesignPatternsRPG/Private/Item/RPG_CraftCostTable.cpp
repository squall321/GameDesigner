// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Item/RPG_CraftCostTable.h"

bool URPG_CraftCostTable::GetUpgradeCost(FGameplayTag RarityTag, int32 FromLevel, FRPG_CraftCostRow& OutRow) const
{
	for (const FRPG_CraftCostRow& Row : UpgradeCosts)
	{
		if (Row.RarityTag == RarityTag && Row.FromLevel == FromLevel)
		{
			OutRow = Row;
			return true;
		}
	}
	return false;
}

bool URPG_CraftCostTable::GetRerollCost(FGameplayTag RarityTag, FRPG_CraftCostRow& OutRow) const
{
	for (const FRPG_CraftCostRow& Row : RerollCosts)
	{
		if (Row.RarityTag == RarityTag)
		{
			OutRow = Row;
			return true;
		}
	}
	return false;
}

bool URPG_CraftCostTable::GetSalvageYield(FGameplayTag RarityTag, TArray<FRPG_ItemStack>& OutMaterials) const
{
	for (const FRPG_SalvageYield& Yield : SalvageYields)
	{
		if (Yield.RarityTag == RarityTag)
		{
			OutMaterials = Yield.Materials;
			return true;
		}
	}
	return false;
}

FName URPG_CraftCostTable::GetDataAssetType_Implementation() const
{
	return FName(TEXT("RPG_CraftCostTable"));
}
