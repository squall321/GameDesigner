// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Flow/Seam_FlowGuard.h"

// Fail-OPEN native default for the flow-guard seam. A guard that does not override CanTransition allows
// every transition (and writes no reason), so registering an empty/half-built guard never deadlocks the
// flow FSM. Concrete guards (e.g. UFlow_ProfileLoadedGuard) override this to veto specific edges.

bool ISeam_FlowGuard::CanTransition_Implementation(FGameplayTag /*From*/, FGameplayTag /*To*/, FGameplayTag& /*OutDenyReason*/) const
{
	return true;
}
