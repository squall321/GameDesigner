// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Grid/Seam_GridCoord.h"
#include "Placement/SimGrid_PlacementTypes.h"
#include "SimGrid_Placeable.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USimGrid_Placeable : public UInterface
{
	GENERATED_BODY()
};

/**
 * Implemented by anything that can be placed onto the SimGrid (a building actor, a furniture object,
 * a turret). The placement system asks the placeable for its footprint and terrain requirements to
 * build an FSimGrid_PlacementContext, then — on the SERVER only — drives OnPlaced/OnRemoved so the
 * placeable can register/unregister itself with the authoritative grid carrier.
 *
 * Footprint and requirement queries are const and client-safe (used for the ghost preview). The
 * lifecycle hooks (OnPlaced/OnRemoved) are AUTHORITY-ONLY: implementers must guard authority and
 * mutate replicated state only on the server. The placement component never calls them on a client.
 */
class DESIGNPATTERNSSIMGRID_API ISimGrid_Placeable
{
	GENERATED_BODY()

public:
	/**
	 * The placeable's footprint as cell offsets (pre-rotation) plus per-cell terrain requirements.
	 * Returned by value so the caller may rotate/translate it freely. Must be deterministic for a
	 * given placeable so client preview and server commit agree.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "SimGrid|Placeable")
	TArray<FSimGrid_FootprintCell> GetFootprint() const;

	/**
	 * Tile-type tags every footprint cell must satisfy in addition to any per-cell RequiredTerrain.
	 * Empty means "no global terrain requirement". The TerrainAllowed rule reads this when no explicit
	 * allow-list is configured on the rule itself.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "SimGrid|Placeable")
	FGameplayTagContainer GetRequiredTileTypes() const;

	/**
	 * AUTHORITY ONLY. Called on the server after the placement is committed, with the final origin and
	 * rotation. The placeable should claim its cells on the grid carrier here. Implementers MUST guard
	 * authority and early-return on clients.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "SimGrid|Placeable")
	void OnPlaced(const FSeam_CellCoord& Origin, ESimGrid_Rotation Rotation);

	/**
	 * AUTHORITY ONLY. Called on the server when the placeable is removed from the grid, so it can
	 * release its claimed cells. Implementers MUST guard authority and early-return on clients.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "SimGrid|Placeable")
	void OnRemoved();
};
