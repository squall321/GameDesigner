// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "Stats/RPG_StatsComponent.h"   // ERPG_StatModOp
#include "Item/RPG_ItemInstance.h"      // FRPG_ItemAffix
#include "RPG_AffixDefinition.generated.h"

class UCurveFloat;

/**
 * Definition of one rollable affix.
 *
 * Identity is the inherited DataTag (resolved load-free through the data registry). A roll produces a
 * concrete FRPG_ItemAffix by sampling MagnitudeRange (optionally scaled by item level through
 * MagnitudeByItemLevel) using a CALLER-SUPPLIED FRandomStream, so the roll is deterministic and reproducible
 * on server and on a preview client given the same seed. BudgetCost lets the roller spend a rarity-derived
 * affix budget; AllowedItemTypes and RarityRequirement gate which items can receive this affix.
 *
 * No magic numbers: every range/curve/budget is authored on the asset.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSRPG_API URPG_AffixDefinition : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	URPG_AffixDefinition();

	/** Attribute this affix modifies. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Affix")
	FGameplayTag AttributeTag;

	/** Combine operation applied when this affix folds into a stat. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Affix")
	ERPG_StatModOp Op = ERPG_StatModOp::Additive;

	/** Inclusive magnitude range sampled at roll time (before any item-level scaling). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Affix")
	FFloatRange MagnitudeRange;

	/**
	 * Optional curve scaling the rolled magnitude by item level (X = item level, Y = multiplier). When
	 * absent the rolled magnitude is used directly (multiplier defaults to 1).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Affix")
	TObjectPtr<UCurveFloat> MagnitudeByItemLevel;

	/** Affix-budget cost charged to the rarity budget when this affix is selected. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Affix", meta = (ClampMin = "0"))
	int32 BudgetCost = 1;

	/** Item-type tags this affix may roll on (empty = any). Matched against URPG_ItemDefinition::ItemTypeTag. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Affix")
	FGameplayTagContainer AllowedItemTypes;

	/** Rarity requirement; the item's rolled rarity tag must satisfy this query (empty query = any). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Affix")
	FGameplayTagQuery RarityRequirement;

	/** Selection weight inside the roller's weighted pick (relative to other candidate affixes). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RPG|Affix", meta = (ClampMin = "0.0"))
	float SelectionWeight = 1.f;

	/** True if ItemTypeTag is permitted by AllowedItemTypes. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Affix")
	bool AllowsItemType(FGameplayTag ItemTypeTag) const;

	/** True if RarityTag satisfies RarityRequirement. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Affix")
	bool AllowsRarity(FGameplayTag RarityTag) const;

	/**
	 * Roll a concrete affix at ItemLevel using Stream (deterministic). The returned affix carries this
	 * definition's DataTag, attribute and op, and the sampled+scaled magnitude wrapped in an FSeam_NetValue.
	 */
	FRPG_ItemAffix Roll(int32 ItemLevel, FRandomStream& Stream) const;

	//~ Begin UDP_DataAsset
	/** Collapse all affix definitions into one shared "RPG_Affix" asset-manager bucket. */
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset
};
