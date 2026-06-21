// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "Resource/USurv_ResourceStoreComponent.h"
#include "USurv_TechNode.generated.h"

/**
 * One node of the survival tech / research tree, identified by the core data registry via its
 * inherited DataTag (which IS the tech tag granted when this node is researched — e.g.
 * "Surv.Tech.Smithing").
 *
 * Purely data: it lists the prerequisite tech tags that must already be known, the recipes/stations
 * it unlocks, and the cost/time to research it. Recipes are referenced by tag (resolved through the
 * registry) so USurv_Recipe stays untouched. The KnowledgeComponent consults these nodes to validate
 * a research request; the AdvancedCraftingComponent consults them to gate craftable lists.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSSURVIVAL_API USurv_TechNode : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/**
	 * Tech tags that must ALL be known before this node can be researched. Empty = a root node with
	 * no prerequisites. Matching is exact-tag-set membership against the player's known-tech ledger.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Survival|Tech")
	FGameplayTagContainer PrerequisiteTechTags;

	/**
	 * Recipe tags this node unlocks for crafting once researched. The advanced crafting component
	 * treats a recipe as tech-gated if any tech node lists it here.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Survival|Tech")
	FGameplayTagContainer UnlocksRecipeTags;

	/** Station tags this node unlocks (informational; station availability is set at runtime). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Survival|Tech")
	FGameplayTagContainer UnlocksStationTags;

	/** Resources consumed to research this node (paid from the player's resource store). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Survival|Tech")
	TArray<FSurv_ResourceStack> ResearchCost;

	/** Seconds of research time once started (0 = instant). Drives the knowledge component's timer. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Survival|Tech", meta = (ClampMin = "0"))
	int32 ResearchTimeSeconds = 0;

	/** The tech tag granted on completion. Defaults to this asset's DataTag when left unset. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Survival|Tech")
	FGameplayTag GrantedTechTag;

	/** The effective tech tag this node grants (GrantedTechTag if set, otherwise the DataTag). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Survival|Tech")
	FGameplayTag GetGrantedTechTag() const { return GrantedTechTag.IsValid() ? GrantedTechTag : DataTag; }

	/** Groups tech nodes under one asset-manager bucket for registry enumeration. */
	virtual FName GetDataAssetType_Implementation() const override { return FName(TEXT("Surv_TechNode")); }
};
