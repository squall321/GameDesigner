// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "SimGrid_DeveloperSettings.generated.h"

/**
 * Project-wide configuration for the SimGrid module. Appears under
 * Project Settings -> Plugins -> Design Patterns SimGrid. These are the only place SimGrid keeps
 * gameplay tunables — none are hardcoded in code. This is a plain UDeveloperSettings (NOT a
 * UDP_DeveloperSettings) with its own static Get(), so the SimGrid module owns its own config record.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Design Patterns SimGrid"))
class DESIGNPATTERNSSIMGRID_API USimGrid_DeveloperSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	USimGrid_DeveloperSettings();

	/** Group these settings under the editor's "Plugins" category. */
	virtual FName GetCategoryName() const override { return FName("Plugins"); }

	/** Convenience accessor for the project's single config instance. Never null in a configured project. */
	static const USimGrid_DeveloperSettings* Get();

	/**
	 * Edge length, in world units, of one grid cell when a world has no other source of truth. The
	 * world subsystem exposes this through ISeam_TileProviderRead::GetCellSize.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "SimGrid|Layout", meta = (ClampMin = "1.0", Units = "cm"))
	float DefaultCellSize = 100.f;

	/**
	 * Number of cells per side in a replication chunk. Each chunk maps to one
	 * ASimGrid_ChunkReplicator carrier; larger chunks mean fewer carriers but coarser dormancy.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "SimGrid|Layout", meta = (ClampMin = "1"))
	FIntPoint DefaultChunkSize = FIntPoint(16, 16);

	/**
	 * Service-locator key the world subsystem registers itself under as the read-only tile provider.
	 * Consumers (agents, economy) resolve the grid by this tag instead of depending on SimGrid.
	 * Anchor under DP.Service (e.g. "DP.Service.SimGrid.TileProvider").
	 */
	UPROPERTY(EditAnywhere, Config, Category = "SimGrid|Services")
	FGameplayTag TileProviderServiceTag;

	/** Hard ceiling on cells visited by a single flood-fill query, to bound worst-case cost. */
	UPROPERTY(EditAnywhere, Config, Category = "SimGrid|Limits", meta = (ClampMin = "1"))
	int32 MaxFloodFillCells = 4096;

	/** Hard ceiling, in cells, on the radius accepted by ring/disc queries. */
	UPROPERTY(EditAnywhere, Config, Category = "SimGrid|Limits", meta = (ClampMin = "1"))
	int32 MaxQueryRadius = 256;

	/** Hard ceiling, in cells, on the length of a single rasterised line query. */
	UPROPERTY(EditAnywhere, Config, Category = "SimGrid|Limits", meta = (ClampMin = "1"))
	int32 MaxLineLength = 1024;

	/**
	 * Optional finite bounds for the grid, in cells (inclusive min, exclusive max on each axis). When
	 * bUseBounds is false the grid is treated as unbounded and IsValidCell is always true.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "SimGrid|Layout")
	bool bUseBounds = false;

	/** Inclusive minimum cell coordinate when bUseBounds is true. */
	UPROPERTY(EditAnywhere, Config, Category = "SimGrid|Layout", meta = (EditCondition = "bUseBounds"))
	FIntPoint BoundsMinCell = FIntPoint(-1024, -1024);

	/** Exclusive maximum cell coordinate when bUseBounds is true. */
	UPROPERTY(EditAnywhere, Config, Category = "SimGrid|Layout", meta = (EditCondition = "bUseBounds"))
	FIntPoint BoundsMaxCell = FIntPoint(1024, 1024);

	/** Returns a sanitized chunk size with each axis clamped to at least 1. */
	FIntPoint GetSafeChunkSize() const
	{
		return FIntPoint(FMath::Max(1, DefaultChunkSize.X), FMath::Max(1, DefaultChunkSize.Y));
	}
};
