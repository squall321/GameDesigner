// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "RPG_LoreDataAsset.generated.h"

/**
 * One immutable lore entry: a codex page unlocked by gameplay/quests.
 *
 * Content is data; the UNLOCK STATE is a world-hub flag (so it replicates + saves through the hub's single
 * path), addressed by this entry's LoreTag under the DP.WorldHub.RPG.Lore root. The journal aggregator
 * surfaces unlocked entries by checking those flags.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSRPG_API FRPG_LoreEntry
{
	GENERATED_BODY()

	/** Identity of this lore entry (also the hub flag key under the lore root). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Journal|Lore", meta = (Categories = "RPG.Lore"))
	FGameplayTag LoreTag;

	/** Category this entry files under (e.g. RPG.Lore.Category.History) for journal tab grouping. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Journal|Lore")
	FGameplayTag CategoryTag;

	/** Player-facing title. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Journal|Lore")
	FText Title;

	/** Player-facing body text. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Journal|Lore", meta = (MultiLine = "true"))
	FText Body;

	/** Optional sort order within its category. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Journal|Lore")
	int32 SortOrder = 0;
};

/**
 * A bundle of lore entries authored as a tag-keyed data asset (resolved by DataTag through the registry).
 *
 * A project may ship one bundle per region/category. The journal aggregator reads bundles and filters them
 * to the ones whose unlock flag is set in the world hub.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSRPG_API URPG_LoreDataAsset : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/** The lore entries in this bundle. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Journal|Lore")
	TArray<FRPG_LoreEntry> Entries;

	//~ Begin UDP_DataAsset
	/** Groups all lore bundles under one asset-manager type bucket ("RPG_Lore"). */
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset

	/** @return the entry with LoreTag, or null. */
	const FRPG_LoreEntry* FindEntry(FGameplayTag LoreTag) const;
};
