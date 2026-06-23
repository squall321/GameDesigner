// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Jobs/Seam_JobReservation.h"

// INERT native defaults for the reservation seam. With no reservation subsystem present (the seam
// unresolved), TryReserve always succeeds optimistically so the consumer can still act, IsReserved
// reports "free", and Release is a no-op — i.e. reservation is simply not enforced. The SimAgents
// reservation subsystem overrides all three to enforce single-claim semantics on its replicated carrier.

bool ISeam_JobReservation::TryReserve_Implementation(FSeam_EntityId /*Target*/, FSeam_EntityId /*Agent*/)
{
	// Optimistic default: without an enforcer, allow the claim so behaviour still proceeds.
	return true;
}

void ISeam_JobReservation::Release_Implementation(FSeam_EntityId /*Target*/)
{
	// No enforcer => nothing to release.
}

bool ISeam_JobReservation::IsReserved_Implementation(FSeam_EntityId /*Target*/) const
{
	// No enforcer => never reserved.
	return false;
}
