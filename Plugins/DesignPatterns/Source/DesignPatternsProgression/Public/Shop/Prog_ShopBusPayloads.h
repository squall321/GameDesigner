// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Prog_ShopBusPayloads.generated.h"

/**
 * Purchase-outcome notification, broadcast on ProgTags::Bus_ShopPurchased after TryPurchase resolves
 * (server-side; the client learns the result through its own OnPurchaseResult delegate). Lets HUD,
 * analytics, vendor barks and SFX react to a sale without binding to the vendor component directly.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSPROGRESSION_API FProg_ShopPurchaseEvent
{
	GENERATED_BODY()

	/** The vendor actor whose shop was purchased from. */
	UPROPERTY(BlueprintReadOnly, Category = "Progression|Shop")
	TWeakObjectPtr<AActor> Vendor = nullptr;

	/** The buyer actor. */
	UPROPERTY(BlueprintReadOnly, Category = "Progression|Shop")
	TWeakObjectPtr<AActor> Buyer = nullptr;

	/** Index of the purchased entry within the shop definition. */
	UPROPERTY(BlueprintReadOnly, Category = "Progression|Shop")
	int32 EntryIndex = INDEX_NONE;

	/** Item that was (or would have been) delivered. */
	UPROPERTY(BlueprintReadOnly, Category = "Progression|Shop")
	FGameplayTag ItemTag;

	/** Quantity delivered (0 on failure). */
	UPROPERTY(BlueprintReadOnly, Category = "Progression|Shop")
	int32 GrantedCount = 0;

	/** Currency spent. */
	UPROPERTY(BlueprintReadOnly, Category = "Progression|Shop")
	FGameplayTag PriceCurrency;

	/** Amount spent (0 on failure). */
	UPROPERTY(BlueprintReadOnly, Category = "Progression|Shop")
	int64 PricePaid = 0;

	/** True only on a fully successful purchase. */
	UPROPERTY(BlueprintReadOnly, Category = "Progression|Shop")
	bool bSuccess = false;
};
