// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Items/Seam_ItemDurability.h"

// Default (unoverridden) implementations. When no real durability provider is present, reads fail closed
// (treated as broken / zero durability) and mutations no-op, so a project without the Survival durability
// component still runs deterministically. The real implementer (USurv_DurabilityComponent adapter, or an
// item-instance fallback) overrides these.

float ISeam_ItemDurability::GetDurabilityNormalized_Implementation() const
{
	return 0.f; // unknown durability -> treated as worn/broken (fail-closed)
}

bool ISeam_ItemDurability::IsBroken_Implementation() const
{
	return true; // no provider -> assume broken so callers do not over-trust an unmanaged item
}

void ISeam_ItemDurability::ApplyWear_Implementation(float /*Amount*/)
{
}

void ISeam_ItemDurability::Repair_Implementation(float /*Amount*/)
{
}
