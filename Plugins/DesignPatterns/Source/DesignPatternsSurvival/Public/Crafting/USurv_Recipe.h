// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "Resource/USurv_ResourceStoreComponent.h"
#include "USurv_Recipe.generated.h"

/**
 * A crafting recipe, identified by the core data registry via its inherited DataTag.
 *
 * Reuses the core's tag-identified UDP_DataAsset so recipes resolve through
 * UDP_DataRegistrySubsystem::FindByTag rather than fragile asset paths. Inputs/outputs are
 * expressed as (item-tag, count) stacks matching the lightweight resource store.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSSURVIVAL_API USurv_Recipe : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/** Resource stacks consumed when crafting starts. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Survival|Crafting")
	TArray<FSurv_ResourceStack> Inputs;

	/** Resource stacks produced when crafting completes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Survival|Crafting")
	TArray<FSurv_ResourceStack> Outputs;

	/** Seconds the craft takes to complete once started. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Survival|Crafting", meta = (ClampMin = "0.0"))
	float CraftTimeSeconds = 2.f;

	/**
	 * Station tag this recipe requires (e.g. Surv.Station.Forge). Empty means craftable anywhere
	 * (hand-craft). Matching is hierarchy-aware: a station providing "Surv.Station.Forge" satisfies
	 * a recipe requiring "Surv.Station".
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Survival|Crafting")
	FGameplayTag RequiredStationTag;

	/** Groups recipes under one asset-manager bucket so the registry/asset manager can enumerate them. */
	virtual FName GetDataAssetType_Implementation() const override { return FName(TEXT("Surv_Recipe")); }
};
