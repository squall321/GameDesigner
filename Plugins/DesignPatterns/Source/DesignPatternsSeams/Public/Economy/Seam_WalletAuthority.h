// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Seam_WalletAuthority.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_WalletAuthority : public UInterface
{
	GENERATED_BODY()
};

/**
 * AUTHORITY-ONLY WRITE dual of the read-only ISeam_Wallet seam.
 *
 * ISeam_Wallet intentionally exposes only reads (GetBalance/CanAfford) so that nothing can spend a
 * player's currency through a generic read seam. But cross-module authoritative systems — a merchant
 * trade flow, an auction settlement, a bank deposit, a quest-reward payout — genuinely need to debit
 * and credit currency without hard-depending on the concrete wallet module. This seam fills that gap:
 * it is the single, server-side, validated mutation path, implemented additively by the concrete
 * wallet component (which already owns the replicated balances and authority guards).
 *
 * CONTRACT:
 *   - CanSpend is const/read-only and safe anywhere (it just asks "would Spend succeed?").
 *   - Spend / Grant are AUTHORITY ONLY. Implementations MUST guard HasAuthority() at the TOP and
 *     return false / 0 on clients. A client never reaches these directly: a player-owned component
 *     issues a Server_* RPC, the server re-derives the amount, then calls Spend/Grant under authority.
 *   - All amounts are whole units (int64); a negative or zero amount is rejected.
 *
 * Resolve it off an actor via GetComponentByInterface(USeam_WalletAuthority) /
 * Implements<USeam_WalletAuthority>() — never by including the wallet's concrete header.
 */
class DESIGNPATTERNSSEAMS_API ISeam_WalletAuthority
{
	GENERATED_BODY()

public:
	/**
	 * True if the wallet currently holds at least Amount of CurrencyTag (i.e. a Spend would succeed).
	 * Const / client-safe; does NOT mutate. A non-positive Amount or invalid tag returns false.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Economy")
	bool CanSpend(FGameplayTag CurrencyTag, int64 Amount) const;

	/**
	 * Debit Amount units of CurrencyTag. AUTHORITY ONLY (no-op + returns false on clients).
	 * Fails (returns false, no mutation) when the tag is invalid, Amount <= 0, or the balance is
	 * insufficient — spending never drives a balance negative. Returns true on a full debit.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Economy")
	bool Spend(FGameplayTag CurrencyTag, int64 Amount);

	/**
	 * Credit Amount units of CurrencyTag. AUTHORITY ONLY (no-op + returns 0 on clients).
	 * Returns the amount actually granted (0 if rejected; may be clamped by a wallet maximum).
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Economy")
	int64 Grant(FGameplayTag CurrencyTag, int64 Amount);
};
