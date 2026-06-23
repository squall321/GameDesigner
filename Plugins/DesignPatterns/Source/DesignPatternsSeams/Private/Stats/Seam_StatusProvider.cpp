// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Stats/Seam_StatusProvider.h"

// Default (unoverridden) implementations. When no real status provider is present (e.g. a project without
// a buff/ability system, or an actor that has no status component), the status bar reads an empty set and
// renders nothing — it never hangs or asserts. The real implementer (the game's status component) overrides
// both methods to project its live effect list.

void ISeam_StatusProvider::GetActiveStatuses_Implementation(TArray<FSeam_StatusEntry>& Out) const
{
	// No statuses by default. Reset so a caller that reuses the array sees an empty result.
	Out.Reset();
}

int32 ISeam_StatusProvider::GetStatusStacks_Implementation(FGameplayTag /*StatusTag*/) const
{
	// No status active by default.
	return 0;
}
