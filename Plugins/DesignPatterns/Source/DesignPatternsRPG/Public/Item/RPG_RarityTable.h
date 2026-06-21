// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "RPG_RarityTable.generated.h"

/**
 * One rarity tier row: a rarity tag plus the budget/affix/socket envelope it grants and its draw weight.
 *
 * The roller draws a rarity with probability proportional to SelectionWeight, then uses AffixBudget /
 * Min/MaxAffixes / SocketCount to shape the rolled item. No magic numbers: all values are authored.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSRPG_API FRPG_RarityTier
{
	GENERATED_BODY()

	/** Identity of this tier (e.g. "RPG.Rarity.Rare"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Rarity")
	FGameplayTag RarityTag;

	/** Affix-point budget the roller may spend across affixes for items of this rarity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Rarity", meta = (ClampMin = "0"))
	int32 AffixBudget = 0;

	/** Minimum number of affixes to roll for this rarity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Rarity", meta = (ClampMin = "0"))
	int32 MinAffixes = 0;

	/** Maximum number of affixes to roll for this rarity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Rarity", meta = (ClampMin = "0"))
	int32 MaxAffixes = 0;

	/** Number of sockets items of this rarity start with. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Rarity", meta = (ClampMin = "0"))
	int32 SocketCount = 0;

	/** Relative weight in the rarity draw (higher = more common). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Rarity", meta = (ClampMin = "0.0"))
	float SelectionWeight = 1.f;

	FRPG_RarityTier() = default;
};

/**
 * Data-driven rarity table mapping each rarity tier to its affix budget, affix count band, socket count and
 * draw weight. Identity = inherited DataTag (so a roller references a table by tag).
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSRPG_API URPG_RarityTable : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/** The authored rarity tiers (order is irrelevant; selection uses weights). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Rarity")
	TArray<FRPG_RarityTier> Tiers;

	/** Find the tier row for RarityTag; returns nullptr if not present. */
	const FRPG_RarityTier* FindTier(const FGameplayTag& RarityTag) const;

	/** Affix budget for RarityTag (0 if unknown). */
	UFUNCTION(BlueprintCallable, Category = "RPG|Rarity")
	int32 GetAffixBudget(FGameplayTag RarityTag) const;

	/** Socket count for RarityTag (0 if unknown). */
	UFUNCTION(BlueprintCallable, Category = "RPG|Rarity")
	int32 GetSocketCount(FGameplayTag RarityTag) const;

	/** Min/max affix count for RarityTag (both 0 if unknown). */
	void GetAffixCountBand(const FGameplayTag& RarityTag, int32& OutMin, int32& OutMax) const;

	/** Weighted rarity draw using Stream; returns an empty tag if no tiers are authored. */
	FGameplayTag RollRarity(FRandomStream& Stream) const;

	//~ Begin UDP_DataAsset
	/** Collapse all rarity tables into one shared "RPG_RarityTable" asset-manager bucket. */
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset
};
