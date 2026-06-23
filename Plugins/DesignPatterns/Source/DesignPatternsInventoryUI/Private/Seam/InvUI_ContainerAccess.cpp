// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Seam/InvUI_ContainerAccess.h"

// Default (unoverridden) implementation. A backend that implements the access gate but does not
// override CanAccess gets the inert default: the container is accessible. This matches how the
// server-side router treats a backend that does NOT implement this interface at all (it is
// unconditionally reachable — see InvUI_ContainerMediatorComponent::CheckAccess and
// InvUI_SpatialIntentComponent), so opting into the interface without overriding leaves behavior
// unchanged. The real access perimeter is an explicit CanAccess override; the router still performs
// its own authority and target re-derivation regardless of this default.

bool IInvUI_ContainerAccess::CanAccess_Implementation(AActor* /*Requestor*/) const
{
	return true; // Default: accessible when the gate is not overridden (router still re-derives authority)
}
