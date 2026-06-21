// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Crafting/USurv_CraftingComponent.h"
#include "GameplayTagContainer.h"
#include "USurv_AdvancedCraftingComponent.generated.h"

class USurv_KnowledgeComponent;
class USurv_RecipeAdvancedDef;

/**
 * DEEPENS the base USurv_CraftingComponent ADDITIVELY.
 *
 * The base crafting methods (HandleCraftComplete / TryStartFrontJob) are NON-virtual, so a subclass
 * cannot intercept the base's guaranteed output. Instead this component:
 *
 *  1. Binds the base's OnCraftCompleted delegate and, when a job finishes, layers ADDITIVE advanced
 *     outputs ON TOP of the base's already-deposited Outputs — guaranteed byproducts, a weighted
 *     quality tier (extra stacks), and a possible critical craft (scaled extra stacks). The base
 *     Outputs are always the floor; this only ever ADDS.
 *
 *  2. Adds a knowledge/tech gate on the CLIENT-INTENT path only (ServerStartCraft validates tech +
 *     discovery + station + inputs, then calls the base StartCraft on authority). This gate is
 *     documented as advisory — direct authority calls to the base StartCraft are intentionally left
 *     ungated so existing server logic is never broken.
 *
 * Subclassing the base is purely to inherit its queue/station/store plumbing and reuse its protected
 * helpers; it does NOT override any base method.
 */
UCLASS(ClassGroup = (DesignPatternsSurvival), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSSURVIVAL_API USurv_AdvancedCraftingComponent : public USurv_CraftingComponent
{
	GENERATED_BODY()

public:
	USurv_AdvancedCraftingComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent

	/**
	 * Whether the recipe can be crafted right now by this crafter, considering station, inputs, tech
	 * gate, and discovery. OutReason carries a human-readable failure for UI. Client-safe (read-only).
	 */
	UFUNCTION(BlueprintCallable, Category = "Survival|Crafting")
	bool CanCraftRecipe(FGameplayTag RecipeTag, FText& OutReason) const;

	/** All recipe tags currently craftable (tech/discovery/station/inputs satisfied). For UI lists. */
	UFUNCTION(BlueprintCallable, Category = "Survival|Crafting")
	TArray<FGameplayTag> GetCraftableRecipes() const;

	/**
	 * Client -> server intent to queue a craft. The server re-validates the full gate (tech, discovery,
	 * station, inputs) before calling the base StartCraft on authority.
	 */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerStartCraft(FGameplayTag RecipeTag);

	/** The knowledge ledger consulted for tech / discovery gates. Optional (null = gates pass). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|Crafting")
	TObjectPtr<USurv_KnowledgeComponent> Knowledge;

	/** Optional explicit list of recipe tags this crafter offers (for GetCraftableRecipes). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Survival|Crafting")
	TArray<FGameplayTag> OfferedRecipeTags;

protected:
	/**
	 * Bound to the base OnCraftCompleted. AUTHORITY-side: resolves the same-tag advanced def, rolls
	 * crit + quality tier, and deposits the additive extra/byproduct stacks into the store. No-op on
	 * clients and when no advanced def exists for the recipe.
	 */
	UFUNCTION()
	void HandleAdvancedCompleted(FGameplayTag RecipeTag);

	/** Resolve the same-DataTag advanced extension def for a recipe through the registry (nullable). */
	USurv_RecipeAdvancedDef* ResolveAdvanced(const FGameplayTag& RecipeTag) const;

	/** Evaluate the tech/discovery gate for a recipe given its advanced def (true = passes). */
	bool PassesKnowledgeGate(const USurv_RecipeAdvancedDef* Advanced, FText& OutReason) const;

	/** Weighted-select a quality tier index from the def, or INDEX_NONE if none. Uses Stream for determinism. */
	int32 SelectQualityTier(const USurv_RecipeAdvancedDef* Advanced, FRandomStream& Stream) const;

	/** Broadcast a cosmetic "critical craft" bus notification (local; derived from authority result). */
	void NotifyCriticalCraft(const FGameplayTag& RecipeTag) const;

private:
	/** Tracks whether we have bound OnCraftCompleted, so we bind exactly once. */
	bool bBoundCompleted = false;
};
