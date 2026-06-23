// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Grid/Seam_GridCoord.h"
#include "World/SimGrid_CoordTypes.h"
#include "SimGrid_PathTypes.generated.h"

// IMPORTANT: this header includes World/SimGrid_CoordTypes.h for ESimGrid_Adjacency {Four, Eight} and
// FSimGrid_CoordMath. It must NEVER include Query/SimGrid_QuerySubsystem.h, which declares a DIFFERENT
// enum also named ESimGrid_Adjacency {Orthogonal, EightWay} — the two collide. Path code uses the coord
// types' enum exclusively; the query subsystem is touched only from .cpp where one enum is in scope.

/** How a grid path request terminated. */
UENUM(BlueprintType)
enum class ESimGrid_PathResult : uint8
{
	/** A complete path from start to goal was found. */
	Success,
	/** Start or goal is outside the grid bounds. */
	OutOfBounds,
	/** Start or goal cell is not walkable, so no path can begin/end there. */
	BlockedEndpoint,
	/** The goal is unreachable from the start (walled off). */
	NoPath,
	/** The search hit the node budget (USimGrid_FeatureSettings::MaxPathNodes) before reaching the goal. */
	NodeBudgetExceeded,
	/** No grid provider was available to query. */
	NoGrid
};

/**
 * A computed grid path: the ordered cells from start to goal (inclusive of both), the total traversal
 * cost, and how the request terminated. On failure Cells is empty and Result names the reason.
 *
 * The path is layer-flat: it lives on a single grid layer (the request's layer), projected to flat
 * FSeam_CellCoord. Cross-layer paths are out of scope (they would need a layered traversal seam).
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMGRID_API FSimGrid_PathResult
{
	GENERATED_BODY()

	/** Ordered cells from start (index 0) to goal (last). Empty on any non-Success result. */
	UPROPERTY(BlueprintReadOnly, Category = "SimGrid|Path")
	TArray<FSeam_CellCoord> Cells;

	/** Summed traversal cost across the path (0 when Cells is empty). */
	UPROPERTY(BlueprintReadOnly, Category = "SimGrid|Path")
	float TotalCost = 0.f;

	/** How the request terminated. */
	UPROPERTY(BlueprintReadOnly, Category = "SimGrid|Path")
	ESimGrid_PathResult Result = ESimGrid_PathResult::NoPath;

	/** True when a complete path was produced. */
	bool IsValid() const { return Result == ESimGrid_PathResult::Success && Cells.Num() > 0; }

	/** Number of steps (edges) in the path; 0 for an empty or single-cell path. */
	int32 NumSteps() const { return FMath::Max(0, Cells.Num() - 1); }
};

/**
 * Parameters for a grid path request, all explicit so the same A* core serves walking, hauling and
 * flow-field seeding. Tunable caps come from USimGrid_FeatureSettings; these are the per-request knobs.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMGRID_API FSimGrid_PathRequest
{
	GENERATED_BODY()

	/** Start cell (must be walkable). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimGrid|Path")
	FSeam_CellCoord Start;

	/** Goal cell (must be walkable). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimGrid|Path")
	FSeam_CellCoord Goal;

	/** Which layer of a layered grid to path on. 0 (the base plane) for a flat grid. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimGrid|Path")
	int32 Layer = 0;

	/** 4- or 8-connected movement. Eight enables diagonals (subject to the corner-cut setting). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimGrid|Path")
	ESimGrid_Adjacency Adjacency = ESimGrid_Adjacency::Eight;

	/**
	 * Optional override for the per-cell traversal cost of a cell with no tile-type cost. <= 0 means use
	 * USimGrid_FeatureSettings::DefaultTraversalCost. Lets a caller bias cost without editing settings.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimGrid|Path", meta = (ClampMin = "0.0"))
	float DefaultCostOverride = 0.f;

	FSimGrid_PathRequest() = default;
	FSimGrid_PathRequest(const FSeam_CellCoord& InStart, const FSeam_CellCoord& InGoal)
		: Start(InStart), Goal(InGoal) {}
};
