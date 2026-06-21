// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "Stats/RPG_StatsComponent.h"   // FRPG_StatModifier
#include "RPG_EquipmentSetDefinition.generated.h"

/**
 * One tier of an equipment set's bonuses: "wear at least RequiredPieces members to grant Modifiers".
 *
 * Tiers stack additively as more members are worn (a 2-piece tier and a 4-piece tier both apply at 4 worn).
 * Modifiers are authored as FRPG_StatModifier; the SourceTag on each is overwritten at apply time with the
 * set's derived source key so a set's whole contribution removes/refreshes atomically.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSRPG_API FRPG_SetBonusTier
{
	GENERATED_BODY()

	/** Minimum number of set members that must be worn for this tier to activate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Set", meta = (ClampMin = "1"))
	int32 RequiredPieces = 2;

	/** Modifiers granted while this tier is active. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Set")
	TArray<FRPG_StatModifier> Modifiers;

	FRPG_SetBonusTier() = default;
};

/**
 * Definition of an equipment set: a list of member item identity tags and the tiered bonuses earned by
 * wearing them. Identity = inherited DataTag.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSRPG_API URPG_EquipmentSetDefinition : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/** Item identity tags that count as members of this set. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Set")
	FGameplayTagContainer MemberItems;

	/** Tiered bonuses, keyed by required worn-piece count. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Set")
	TArray<FRPG_SetBonusTier> BonusTiers;

	/** Count how many of EquippedItemTags are members of this set. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Set")
	int32 CountWornMembers(const TArray<FGameplayTag>& EquippedItemTags) const;

	/**
	 * Gather every modifier from tiers whose RequiredPieces <= WornCount into Out (cumulative), stamping
	 * SourceKey onto each modifier's SourceTag so the whole set contribution shares one source group.
	 */
	void GatherActiveBonuses(int32 WornCount, const FGameplayTag& SourceKey, TArray<FRPG_StatModifier>& Out) const;

	//~ Begin UDP_DataAsset
	/** Collapse all set definitions into one shared "RPG_EquipmentSet" asset-manager bucket. */
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset
};
