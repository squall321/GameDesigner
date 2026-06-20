// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Grid/Seam_GridCoord.h"
#include "SimGrid_FlowField.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USimGrid_FlowField : public UInterface
{
	GENERATED_BODY()
};

/**
 * Read seam for a directional flow field over the grid (the classic RTS/crowd pathing structure: a
 * per-cell "best direction toward a goal" plus an integration cost). SimGrid only DEFINES this seam;
 * a game or the SimAgents module provides an implementation that consumers steer against. Keeping it a
 * seam means agents read flow without depending on whoever computed it, and SimGrid can ship its own
 * computed field later behind the same interface.
 *
 * All methods are const and client-safe. Implementations should return a zero direction / sentinel
 * cost for cells outside the field rather than asserting.
 */
class DESIGNPATTERNSSIMGRID_API ISimGrid_FlowField
{
	GENERATED_BODY()

public:
	/** True if a flow direction/cost is available for Cell (it lies inside the field and is reachable). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "SimGrid|FlowField")
	bool HasFlowAt(const FSeam_CellCoord& Cell) const;

	/**
	 * Normalized world-space (XY-plane) direction an agent at Cell should move to follow the field
	 * toward its goal. Returns a zero vector when no flow is available at Cell.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "SimGrid|FlowField")
	FVector GetFlowDirection(const FSeam_CellCoord& Cell) const;

	/**
	 * Integration cost from Cell to the field's goal (lower is closer). Returns a large sentinel
	 * (>= 0) for unreachable / out-of-field cells; callers compare against GetUnreachableCost.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "SimGrid|FlowField")
	float GetIntegrationCost(const FSeam_CellCoord& Cell) const;

	/** The goal cell this field flows toward (for debugging / observers re-deriving the field). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "SimGrid|FlowField")
	FSeam_CellCoord GetGoalCell() const;
};
