// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Net/Seam_NetRelevancyHint.h"

// Conservative native defaults: a half-built implementer reports the Normal tier, no forced
// dormancy and no cull bias, so the tuner falls back to its configured default behaviour.

ESeam_NetRelevancyTier ISeam_NetRelevancyHint::GetRelevancyTier_Implementation() const
{
	return ESeam_NetRelevancyTier::Normal;
}

bool ISeam_NetRelevancyHint::WantsDormancyWhenIdle_Implementation() const
{
	return false;
}

float ISeam_NetRelevancyHint::GetCullDistanceBias_Implementation() const
{
	return 0.f;
}
