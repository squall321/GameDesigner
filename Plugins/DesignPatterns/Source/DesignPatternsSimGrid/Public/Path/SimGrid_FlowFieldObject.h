// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/ScriptInterface.h"
#include "Grid/Seam_GridCoord.h"
#include "Grid/Seam_TileProviderRead.h"
#include "Interfaces/SimGrid_FlowField.h"
#include "World/SimGrid_CoordTypes.h"
#include "SimGrid_FlowFieldObject.generated.h"

class USimGrid_PathQuerySubsystem;

/**
 * A computed directional flow field over a bounded grid region, implementing the read seam
 * ISimGrid_FlowField. This is the classic RTS/crowd structure: a Dijkstra integration of cost FROM every
 * cell TO a single goal, plus a per-cell "best next direction" toward that goal. Many agents share one
 * field and steer by reading GetFlowDirection at their current cell — far cheaper than per-agent A*.
 *
 * The field is built over the WALKABLE cells of a grid (resolved through ISeam_TileProviderRead), reusing
 * USimGrid_PathQuerySubsystem's walkability + per-tile cost rules so flow and A* agree. The build is
 * bounded by a caller-supplied cell window (and the settings' node cap) so it never integrates an
 * unbounded grid. Cells outside the window or unreachable return a zero direction and the unreachable
 * cost sentinel.
 *
 * IMPLEMENTS EXACTLY the four real seam methods (HasFlowAt / GetFlowDirection / GetIntegrationCost /
 * GetGoalCell). The unreachable-cost sentinel is a STATIC helper sourced from USimGrid_FeatureSettings —
 * it is deliberately NOT a seam method and never a literal.
 *
 * Created via NewObject with an outer (the owning subsystem / agent system) so it is GC-managed.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSSIMGRID_API USimGrid_FlowFieldObject : public UObject, public ISimGrid_FlowField
{
	GENERATED_BODY()

public:
	/**
	 * The integration cost meaning "unreachable / outside the field", sourced from
	 * USimGrid_FeatureSettings::UnreachableCostSentinel. Callers compare GetIntegrationCost against this.
	 * Static so consumers share one value without a seam method or a literal.
	 */
	static float GetUnreachableCost();

	/**
	 * Build (or rebuild) the field toward GoalCell over the walkable cells of Grid, restricted to the
	 * inclusive cell window [WindowMin, WindowMax]. Uniform-cost Dijkstra from the goal outward, bounded
	 * by the settings node cap. Adjacency selects 4/8-connected expansion. Returns true if the goal cell
	 * was itself walkable and a field was produced.
	 */
	bool BuildField(const TScriptInterface<ISeam_TileProviderRead>& Grid, const FSeam_CellCoord& GoalCell,
		const FSeam_CellCoord& WindowMin, const FSeam_CellCoord& WindowMax, ESimGrid_Adjacency Adjacency);

	/** Discard the computed field (HasFlowAt becomes false everywhere). */
	void Reset();

	/** Number of cells with a finite integration cost in the current field. */
	UFUNCTION(BlueprintPure, Category = "SimGrid|FlowField")
	int32 GetReachableCellCount() const { return CostByCell.Num(); }

	//~ Begin ISimGrid_FlowField
	virtual bool HasFlowAt_Implementation(const FSeam_CellCoord& Cell) const override;
	virtual FVector GetFlowDirection_Implementation(const FSeam_CellCoord& Cell) const override;
	virtual float GetIntegrationCost_Implementation(const FSeam_CellCoord& Cell) const override;
	virtual FSeam_CellCoord GetGoalCell_Implementation() const override;
	//~ End ISimGrid_FlowField

private:
	/** The goal every cell flows toward. */
	UPROPERTY(Transient)
	FSeam_CellCoord Goal;

	/** Was a field successfully built. */
	UPROPERTY(Transient)
	bool bBuilt = false;

	/** Integration cost from each reachable cell to the goal (lower is closer). Absence == unreachable. */
	TMap<FSeam_CellCoord, float> CostByCell;

	/** Per-cell best next-step cell toward the goal (the lowest-cost reachable neighbour). */
	TMap<FSeam_CellCoord, FSeam_CellCoord> NextByCell;

	/** The world-space cell size cached from the grid at build time, for direction vectors. */
	float CachedCellSize = 100.f;
};
