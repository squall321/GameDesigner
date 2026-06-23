// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "Seam/HUD_Trackable.h"
#include "Objective/Seam_ObjectiveSource.h" // FSeam_ObjectiveSnapshot
#include "HUD_ObjectiveTrackable.generated.h"

/**
 * Lightweight objective -> world-marker bridge registered into the world UHUD_MarkerRegistrySubsystem so a
 * pinned objective with a world location shows on the minimap / world-indicator layers, WITHOUT the quest
 * system knowing about markers.
 *
 * It implements IHUD_Trackable by projecting an FSeam_ObjectiveSnapshot: GetWorldLocation returns the
 * snapshot location, GetMarkerTag returns the configured objective marker kind, and IsVisibleOnMap returns
 * true only while the snapshot carries a world location and the objective is still tracked.
 *
 * Because the registry holds trackables WEAKLY, the owning subsystem MUST keep these alive in an owning
 * UPROPERTY array and unregister + drop them on teardown (done by UHUD_ObjectiveTrackerSubsystem).
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSHUD_API UHUD_ObjectiveTrackable : public UObject, public IHUD_Trackable
{
	GENERATED_BODY()

public:
	//~ Begin IHUD_Trackable
	virtual FVector GetWorldLocation_Implementation() const override;
	virtual FGameplayTag GetMarkerTag_Implementation() const override;
	virtual bool IsVisibleOnMap_Implementation() const override;
	//~ End IHUD_Trackable

	/** Update the projected objective snapshot (and visibility). */
	void SetSnapshot(const FSeam_ObjectiveSnapshot& InSnapshot);

	/** Set the marker kind tag used for this objective's world marker (e.g. HUDTags::Marker_Objective). */
	void SetMarkerTag(FGameplayTag InMarkerTag) { MarkerTag = InMarkerTag; }

	/** The objective id this bridge tracks (echoed from the snapshot for correlation). */
	FGameplayTag GetObjectiveId() const { return Snapshot.ObjectiveId; }

private:
	/** The current projected objective snapshot. */
	UPROPERTY(Transient)
	FSeam_ObjectiveSnapshot Snapshot;

	/** The marker kind tag returned by GetMarkerTag. */
	UPROPERTY(Transient)
	FGameplayTag MarkerTag;
};
