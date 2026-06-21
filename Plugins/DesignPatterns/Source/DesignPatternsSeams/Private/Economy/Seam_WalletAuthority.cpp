// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Economy/Seam_WalletAuthority.h"

/**
 * Default BlueprintNativeEvent bodies for ISeam_WalletAuthority. The seam carries no behaviour of its
 * own — the concrete wallet component overrides every method. These fail-closed defaults exist only so
 * an object that has not overridden them never debits or grants currency.
 */

bool ISeam_WalletAuthority::CanSpend_Implementation(FGameplayTag /*CurrencyTag*/, int64 /*Amount*/) const
{
	return false;
}

bool ISeam_WalletAuthority::Spend_Implementation(FGameplayTag /*CurrencyTag*/, int64 /*Amount*/)
{
	return false;
}

int64 ISeam_WalletAuthority::Grant_Implementation(FGameplayTag /*CurrencyTag*/, int64 /*Amount*/)
{
	return 0;
}
