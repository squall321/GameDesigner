// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Objective/Seam_ObjectiveSource.h"

// Default (unoverridden) implementations. When no real objective source is present (a project without a
// quest system, or before one registers), the HUD objective tracker reads an empty set and renders nothing.
// The real implementer (the game's quest/hub adapter published under the project's objective-source service
// key) overrides both methods to project its live tracked-objective list.

void ISeam_ObjectiveSource::GetTrackedObjectives_Implementation(TArray<FSeam_ObjectiveSnapshot>& Out) const
{
	// Nothing tracked by default. Reset so a caller that reuses the array sees an empty result.
	Out.Reset();
}

bool ISeam_ObjectiveSource::GetObjectiveById_Implementation(FGameplayTag /*Id*/, FSeam_ObjectiveSnapshot& Out) const
{
	// Unknown objective by default.
	Out = FSeam_ObjectiveSnapshot();
	return false;
}
