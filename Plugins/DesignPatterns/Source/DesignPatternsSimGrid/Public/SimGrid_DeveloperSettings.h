// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "SimGrid_DeveloperSettings.generated.h"

/**
 * Project-wide configuration for the SimGrid module. Appears under
 * Project Settings -> Plugins -> Design Patterns SimGrid.
 *
 * Every spatial query in USimGrid_QuerySubsystem is hard-bounded by the caps declared here so a
 * bad radius or an unbounded flood-fill can never spin the game thread. The defaults are conservative
 * tunables (not gameplay magic numbers baked into code) — designers raise them per project as needed.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Design Patterns SimGrid"))
class DESIGNPATTERNSSIMGRID_API USimGrid_DeveloperSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	USimGrid_DeveloperSettings();

	virtual FName GetCategoryName() const override { return FName("Plugins"); }

	/** Convenience accessor for the CDO. Never null in a configured project. */
	static const USimGrid_DeveloperSettings* Get();

	// --- Spatial query caps (USimGrid_QuerySubsystem) ---

	/**
	 * Hard cap on the radius (in cells) any region/line/neighbor query will honour. Larger requested
	 * radii are clamped to this value. Bounds the worst-case cell count a single query can visit.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Query", meta = (ClampMin = "1"))
	int32 MaxQueryRadiusCells = 64;

	/**
	 * Hard cap on the number of cells any single region/region-shape query may COLLECT into its result.
	 * Independent of radius so even a huge shape can be bounded; the query stops collecting past this.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Query", meta = (ClampMin = "1"))
	int32 MaxRegionCells = 4096;

	/**
	 * Hard cap on cells a single flood-fill may VISIT before it stops and returns what it has. Protects
	 * against pathological open regions or mis-specified match predicates.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Query", meta = (ClampMin = "1"))
	int32 MaxFloodFillCells = 8192;

	/**
	 * Hard cap on the number of cells a single line (Bresenham) query may emit. Bounds long lines on
	 * huge grids.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Query", meta = (ClampMin = "1"))
	int32 MaxLineCells = 1024;

	// --- Placement caps (USimGrid_PlacementComponent / rules) ---

	/**
	 * Maximum number of footprint cells a single placement may contain. The server rejects a committed
	 * placement whose footprint exceeds this, so a malicious client cannot submit an enormous footprint.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Placement", meta = (ClampMin = "1"))
	int32 MaxFootprintCells = 256;
};
