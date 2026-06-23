// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "Analytics_HeatmapSubsystem.generated.h"

class UAnalytics_Subsystem;
class UAnalytics_TelemetryDataAsset;

/**
 * One occupied heatmap bucket: an integer XY cell index plus an accumulated weight.
 *
 * PLAIN copyable value (no UObject refs), so a snapshot of all buckets can be handed to a worker
 * thread for file export without touching the UObject graph off the game thread — mirroring
 * FAnalytics_BufferedEvent / FlushToFileSink.
 */
USTRUCT()
struct FAnalytics_HeatBucket
{
	GENERATED_BODY()

	/** Bucket X index (WorldPos.X / bucket size, floored). */
	UPROPERTY()
	int32 X = 0;

	/** Bucket Y index (WorldPos.Y / bucket size, floored). */
	UPROPERTY()
	int32 Y = 0;

	/** Accumulated weight (sum of RecordSpatialEvent weights that fell in this cell). */
	UPROPERTY()
	int32 Count = 0;
};

/**
 * World-scoped spatial / heatmap telemetry subsystem.
 *
 * RecordSpatialEvent buckets a world position into a uniform XY grid (bucket size + max-bucket cap
 * from the validated data-asset accessors, never 0) per category tag, accumulating weight per cell.
 * ExportHeatmap takes a GAME-THREAD plain copy of one category's bucket grid into a
 * TArray<FAnalytics_HeatBucket> and hands ONLY that copy to Async(EAsyncExecution::ThreadPool) for a
 * CSV file write (capture BY VALUE, never 'this'), exactly mirroring FlushToFileSink, and records an
 * aggregate export marker through the consent-gated core subsystem.
 *
 * GC / lifetime (HARD RULE 3 + 5): a world subsystem referencing the GI-scoped core analytics
 * subsystem across the world boundary holds it as a TWeakObjectPtr and re-resolves it via
 * FDP_SubsystemStatics::GetGameInstanceSubsystem with a GetGameInstance null-guard. On Deinitialize
 * it performs a FINAL synchronous export of every non-empty category so a world teardown does not
 * drop accumulated spatial data.
 *
 * Positions are handed IN by callers; the subsystem never reaches into World Partition / streaming.
 * Everything is local/per-machine; nothing replicates or saves.
 */
UCLASS()
class DESIGNPATTERNSANALYTICS_API UAnalytics_HeatmapSubsystem : public UDP_WorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/**
	 * Accumulate Weight at WorldPos for CategoryTag. The XY position is bucketed by the data-asset
	 * bucket size; Z is ignored (top-down heatmap). A new cell is only created while the per-category
	 * bucket count is under the configured cap (bounded memory); over the cap, only existing cells
	 * accumulate. Weight <= 0 is treated as 1 (defensive). Game thread only.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Analytics|Heatmap")
	void RecordSpatialEvent(FGameplayTag CategoryTag, const FVector& WorldPos, int32 Weight = 1);

	/**
	 * Export one category's grid: snapshot on the game thread, write CSV off-thread (by value), and
	 * record an Analytics.Event.Heatmap.Export marker (category + non-empty bucket count) through the
	 * consent-gated core subsystem. Does NOT clear the category (call ClearCategory to reset).
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Analytics|Heatmap")
	void ExportHeatmap(FGameplayTag CategoryTag);

	/** Accumulated weight in the cell containing WorldPos for CategoryTag (0 if none). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Analytics|Heatmap")
	int32 GetBucketCount(FGameplayTag CategoryTag, const FVector& WorldPos) const;

	/** Drop all accumulated buckets for a category. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Analytics|Heatmap")
	void ClearCategory(FGameplayTag CategoryTag);

	//~ Begin UDP_WorldSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_WorldSubsystem

private:
	/** Packed XY cell key. Combines two int32 cell indices into one 64-bit map key. */
	static uint64 MakeCellKey(int32 X, int32 Y)
	{
		return (static_cast<uint64>(static_cast<uint32>(X)) << 32) | static_cast<uint64>(static_cast<uint32>(Y));
	}

	/** One category's grid: cell key -> bucket. */
	struct FHeatGrid
	{
		TMap<uint64, FAnalytics_HeatBucket> Cells;
	};

	/** All categories' grids. Not a UPROPERTY: plain POD, no UObject refs, off-thread-copyable. */
	TMap<FGameplayTag, FHeatGrid> Categories;

	/** Weakly-held core analytics subsystem (cross-world ref; re-resolved + pruned on use). */
	TWeakObjectPtr<UAnalytics_Subsystem> CachedAnalyticsSubsystem;

	/** Lazily-resolved telemetry data asset (weak; owned by the asset manager). */
	TWeakObjectPtr<const UAnalytics_TelemetryDataAsset> CachedDataAsset;

	/** True once we have attempted to resolve the data asset. */
	bool bDataAssetResolutionAttempted = false;

	/** Re-resolve the GI-scoped core analytics subsystem with a GetGameInstance null-guard. May be null. */
	UAnalytics_Subsystem* ResolveAnalyticsSubsystem();

	/** Resolve (and cache) the telemetry data asset, or null. */
	const UAnalytics_TelemetryDataAsset* ResolveDataAsset();

	/** Snapshot a category's grid into a flat array (game thread) for off-thread export. */
	void SnapshotCategory(const FGameplayTag& CategoryTag, TArray<FAnalytics_HeatBucket>& OutBuckets) const;

	/** Build the export file path for a category (game thread; FPaths is game-thread-friendly). */
	FString BuildExportPath(const FGameplayTag& CategoryTag) const;

	/** Write a snapshot to CSV off the game thread (capture by value), or synchronously on shutdown. */
	void WriteSnapshot(const TArray<FAnalytics_HeatBucket>& Snapshot, const FString& FullPath, bool bSynchronous);
};
