// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Objective/HUD_ObjectiveTrackable.h"

FVector UHUD_ObjectiveTrackable::GetWorldLocation_Implementation() const
{
	return Snapshot.WorldLocation;
}

FGameplayTag UHUD_ObjectiveTrackable::GetMarkerTag_Implementation() const
{
	return MarkerTag;
}

bool UHUD_ObjectiveTrackable::IsVisibleOnMap_Implementation() const
{
	// Only show a world marker while the objective is tracked AND carries a world location.
	return Snapshot.IsValidObjective() && Snapshot.bHasWorldLocation;
}

void UHUD_ObjectiveTrackable::SetSnapshot(const FSeam_ObjectiveSnapshot& InSnapshot)
{
	Snapshot = InSnapshot;
}
