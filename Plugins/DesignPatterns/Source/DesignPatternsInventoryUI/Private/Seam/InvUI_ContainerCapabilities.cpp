// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Seam/InvUI_ContainerCapabilities.h"

// Default (unoverridden) implementations. A backend that does not implement the capabilities seam
// advertises nothing, so the UI falls back to its own conservative defaults instead of crashing.

void IInvUI_ContainerCapabilities::GetCapabilities_Implementation(FGameplayTagContainer& Out) const
{
	Out.Reset();
}

bool IInvUI_ContainerCapabilities::HasCapability_Implementation(FGameplayTag /*Cap*/) const
{
	return false;
}
