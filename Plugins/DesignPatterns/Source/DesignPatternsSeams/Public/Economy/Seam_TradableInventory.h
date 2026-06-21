// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Seam_TradableInventory.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_TradableInventory : public UInterface
{
	GENERATED_BODY()
};

/**
 * Authority-only WRITE seam to REMOVE tradeable items from a player's real inventory — the missing dual of
 * ISeam_PurchaseTarget::GrantItem (which delivers). Auctions and player-to-player trade escrow items by
 * removing them here into a server-only ledger, so the economy module never includes the RPG inventory.
 * The RPG inventory component ships the adapter.
 *
 * CanRemove is a const, client-safe pre-check. RemoveItem is AUTHORITY ONLY: the implementer guards
 * authority and no-ops (returns 0) on clients.
 */
class DESIGNPATTERNSSEAMS_API ISeam_TradableInventory
{
	GENERATED_BODY()

public:
	/** True if at least Count of ItemTag can currently be removed (present and tradeable/not soulbound). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Economy")
	bool CanRemove(FGameplayTag ItemTag, int32 Count) const;

	/** Remove Count of ItemTag. Returns the count actually removed (0 on failure). AUTHORITY ONLY. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Economy")
	int32 RemoveItem(FGameplayTag ItemTag, int32 Count);
};
