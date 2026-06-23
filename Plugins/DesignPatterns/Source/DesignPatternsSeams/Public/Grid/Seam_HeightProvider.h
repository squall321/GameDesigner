// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Grid/Seam_GridCoord.h"
#include "Seam_HeightProvider.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_HeightProvider : public UInterface
{
	GENERATED_BODY()
};

/**
 * Read-only seam supplying per-cell terrain HEIGHT, which the flat tile seam (ISeam_TileProviderRead)
 * deliberately omits — it models cells as a 2D plane with no Z. A grid implementation that samples the
 * world (e.g. a downward trace adapter) publishes this so consumers (camera framing, ballistics,
 * line-of-sight, flow fields that want slope cost) get a cell's ground elevation without depending on
 * SimGrid or re-running their own traces.
 *
 * All methods are const and client-safe. Heights are in world units (cm). When a cell's height is not
 * known on this machine (e.g. nothing was hit by the sampling trace, or the sampler hasn't run), the
 * out-bool reports false and the returned height is the provider's documented fallback (typically the
 * grid plane Z), so callers can choose to treat it as flat ground rather than asserting.
 */
class DESIGNPATTERNSSEAMS_API ISeam_HeightProvider
{
	GENERATED_BODY()

public:
	/**
	 * Ground height (world Z, cm) at the centre of Cell. bOutValid is false when no surface was found
	 * and OutHeight falls back to the grid plane; true when a real surface height was sampled.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Grid")
	float SampleCellHeight(const FSeam_CellCoord& Cell, bool& bOutValid) const;

	/**
	 * Signed height difference (cm) from cell A's ground to cell B's ground (B - A). Positive means B is
	 * higher. Used by movement/flow cost to penalise steep transitions. Falls back to 0 when either cell
	 * has no sampled height.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Grid")
	float GetHeightDelta(const FSeam_CellCoord& A, const FSeam_CellCoord& B) const;

	/** True if a sampled (non-fallback) height is available for Cell on this machine. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Grid")
	bool HasSampledHeight(const FSeam_CellCoord& Cell) const;
};
