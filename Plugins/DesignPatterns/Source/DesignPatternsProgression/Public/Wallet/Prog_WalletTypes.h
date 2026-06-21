// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Prog_WalletTypes.generated.h"

/**
 * Message-bus payload broadcast on DP.Bus.Prog.BalanceChanged whenever an authoritative wallet balance
 * changes (and re-broadcast nowhere on clients — the bus is local, so each peer broadcasts its own).
 *
 * Carried as the inner type of an FInstancedStruct in the message bus, so decoupled listeners (HUD,
 * achievements, analytics) react to currency changes without binding to the wallet component.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSPROGRESSION_API FProg_BalanceChangedEvent
{
	GENERATED_BODY()

	/** The currency whose balance changed. */
	UPROPERTY(BlueprintReadOnly, Category = "Progression|Wallet")
	FGameplayTag Currency;

	/** The post-change balance of that currency. */
	UPROPERTY(BlueprintReadOnly, Category = "Progression|Wallet")
	int64 NewBalance = 0;

	/**
	 * The actor that owns the wallet whose balance changed (e.g. the player state). Weak so the payload
	 * never keeps a dead actor alive; listeners must null-check before use.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Progression|Wallet")
	TWeakObjectPtr<AActor> WalletOwner;

	FProg_BalanceChangedEvent() = default;
};
