// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Seam_PurchaseTarget.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_PurchaseTarget : public UInterface
{
	GENERATED_BODY()
};

/**
 * Authority-only WRITE seam for delivering a purchased item to a player's inventory. The shop debits
 * the wallet server-side, then calls GrantItem on the resolved purchase target. A project-side adapter
 * implements this against the actual inventory backend (RPG inventory, a survival store, etc.) so the
 * shop never hard-depends on a specific inventory module.
 *
 * Implementations MUST guard authority: GrantItem is only ever invoked on the server by the shop after
 * a validated Server_Purchase, but a defensive HasAuthority() check belongs in the implementation.
 */
class DESIGNPATTERNSSEAMS_API ISeam_PurchaseTarget
{
	GENERATED_BODY()

public:
	/** True if this target can currently accept Count of ItemTag (capacity/filter check). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Economy")
	bool CanReceive(FGameplayTag ItemTag, int32 Count) const;

	/** Deliver Count of ItemTag. Returns the count actually granted (0 on failure). AUTHORITY ONLY. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Economy")
	int32 GrantItem(FGameplayTag ItemTag, int32 Count);
};
