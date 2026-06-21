// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "Resource/USurv_ResourceStoreComponent.h"
#include "USurv_RecipeAdvancedDef.generated.h"

/**
 * One craft-quality tier: a quality tag plus the ADDITIVE extra stacks granted ON TOP of the base
 * recipe's guaranteed Outputs when this tier rolls. Tiers are inclusive of the base — the base
 * Outputs are always the floor, and a tier only ever ADDS.
 */
USTRUCT(BlueprintType)
struct FSurv_QualityTier
{
	GENERATED_BODY()

	/** Quality tag for this tier (child of Surv.Quality, e.g. Surv.Quality.Fine). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|Crafting")
	FGameplayTag QualityTag;

	/**
	 * Relative weight used when selecting which tier a (non-critical) craft lands in. Higher weight =
	 * more likely. A tier set with no entries or all-zero weights simply never grants extra stacks.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|Crafting", meta = (ClampMin = "0.0"))
	float SelectionWeight = 1.f;

	/** Extra resource stacks ADDED to the base Outputs when this tier is selected. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|Crafting")
	TArray<FSurv_ResourceStack> ExtraOutputs;

	FSurv_QualityTier() = default;
};

/**
 * Additive, SAME-DataTag extension record for a USurv_Recipe.
 *
 * The base USurv_Recipe is intentionally left untouched (the base crafting component's output methods
 * are non-virtual and cannot be subclassed to intercept output). This def is resolved from the data
 * registry by the SAME DataTag as its recipe and, when present, the advanced crafting component layers
 * ON TOP of the base's guaranteed Outputs: guaranteed byproducts, a weighted quality tier (extra
 * stacks), and a critical-craft chance/multiplier. It also carries the crafting depth gates
 * (tech requirement, discovery requirement) consulted on the client-intent path.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSSURVIVAL_API USurv_RecipeAdvancedDef : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/**
	 * Tech tag that must be known (via the KnowledgeComponent) for this recipe to be craftable.
	 * Empty = no tech gate. Advisory on the base path; enforced on the advanced client-intent path.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Survival|Crafting")
	FGameplayTag GatingTechTag;

	/** When true, the recipe must be DISCOVERED (in the knowledge ledger) before it can be crafted. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Survival|Crafting")
	bool bRequiresDiscovery = false;

	/**
	 * Byproducts ALWAYS deposited in addition to the base Outputs (e.g. slag from smelting). Additive;
	 * never replaces base Outputs.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Survival|Crafting")
	TArray<FSurv_ResourceStack> Byproducts;

	/** 0..1 chance of a CRITICAL craft, which scales the EXTRA (tier + byproduct) stacks. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Survival|Crafting", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CriticalCraftChance = 0.f;

	/** Multiplier applied to extra stacks on a critical craft (>= 1). Base Outputs are never scaled. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Survival|Crafting", meta = (ClampMin = "1.0"))
	float CriticalExtraMultiplier = 2.f;

	/** Quality tiers; one is weighted-selected per craft to grant additive extra stacks. May be empty. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Survival|Crafting")
	TArray<FSurv_QualityTier> QualityTiers;

	/** Shares the base recipe's registry bucket sibling type so advanced defs enumerate separately. */
	virtual FName GetDataAssetType_Implementation() const override { return FName(TEXT("Surv_RecipeAdvanced")); }
};
