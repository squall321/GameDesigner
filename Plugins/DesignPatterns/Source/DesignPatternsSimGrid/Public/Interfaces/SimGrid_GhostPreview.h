// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Grid/Seam_GridCoord.h"
#include "Placement/SimGrid_PlacementTypes.h"
#include "SimGrid_GhostPreview.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USimGrid_GhostPreview : public UInterface
{
	GENERATED_BODY()
};

/**
 * The visual ghost shown while a player drags a placeable around the grid. The placement component
 * owns the ghost as a TScriptInterface and drives it every frame the player moves the cursor; the
 * ghost is a purely CLIENT-SIDE, cosmetic seam (it never touches authoritative state), so the
 * placement system can run its preview without depending on any concrete actor/mesh implementation.
 *
 * A game supplies a ghost (an actor implementing this interface) and hands it to the component via
 * SetGhost. With no ghost set the component still validates; it just shows nothing.
 */
class DESIGNPATTERNSSIMGRID_API ISimGrid_GhostPreview
{
	GENERATED_BODY()

public:
	/**
	 * Show/refresh the ghost at the given origin/rotation with the current validity, so it can tint
	 * itself (green/red/amber for Valid/Invalid/Unknown) and snap to the cell. The full result carries
	 * the resolved and offending cells for fine-grained highlighting.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "SimGrid|Ghost")
	void UpdateGhostPreview(const FSeam_CellCoord& Origin, ESimGrid_Rotation Rotation, const FSimGrid_PlacementResult& Result);

	/** Hide the ghost (placement cancelled or the cursor left the grid). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "SimGrid|Ghost")
	void HideGhostPreview();
};
