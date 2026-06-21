// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Seam_Wallet.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_Wallet : public UInterface
{
	GENERATED_BODY()
};

/**
 * Read-only wallet/currency seam. A player's wallet component implements this; the shop, game-mode
 * rewards, quests, HUD and skill cost-checks read balances through it without depending on the
 * Progression module. Currencies are tag-keyed (soft/hard/event), so any game defines its own.
 *
 * This seam is intentionally read-only: spending/granting currency is an authority-only mutation that
 * lives on the concrete wallet component (guarded), never exposed as a generic seam method that a
 * client could call.
 */
class DESIGNPATTERNSSEAMS_API ISeam_Wallet
{
	GENERATED_BODY()

public:
	/** Current balance of CurrencyTag (0 if the wallet has no such currency). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Economy")
	int64 GetBalance(FGameplayTag CurrencyTag) const;

	/** True if the wallet holds at least Amount of CurrencyTag. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Economy")
	bool CanAfford(FGameplayTag CurrencyTag, int64 Amount) const;

	/** Append every (currency tag -> balance) pair this wallet holds. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Economy")
	void GetAllBalances(TMap<FGameplayTag, int64>& OutBalances) const;
};
