// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Economy/Seam_PriceQuote.h"

/**
 * Default BlueprintNativeEvent bodies for ISeam_PriceQuote. The economy market adapter overrides these.
 * The defaults fail safe: an unimplemented provider prices nothing (0) and applies no multiplier (1.0).
 */

double ISeam_PriceQuote::GetQuotedPrice_Implementation(FGameplayTag /*ItemOrCommodityTag*/) const
{
	return 0.0;
}

float ISeam_PriceQuote::GetPriceMultiplierForFaction_Implementation(FGameplayTag /*FactionTag*/, const AActor* /*Buyer*/) const
{
	return 1.0f;
}
