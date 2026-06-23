// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"

/**
 * Single data-driven tunable home for the six deepening telemetry areas (funnel/cohort, heatmap,
 * economy, performance, breadcrumb, debug dashboard). Identity is the inherited DataTag, resolved
 * through the data registry exactly like UAnalytics_ProgressionDataAsset.
 *
 * Hard rule: NO magic gameplay numbers live in code. Every runtime constant is an EditAnywhere
 * field here with a ClampMin/UIMin meta and a validated Get-Effective-* accessor that applies a
 * documented defensive floor, so a zero/negative config edit can never reach division or sizing
 * math. IsDataValid additionally flags empty / non-monotonic percentile lists and a zero bucket
 * size so designers catch bad data in the editor.
 *
 * Everything here is per-machine/local. Nothing in this asset replicates or saves into gameplay
 * save state.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSANALYTICS_API UAnalytics_TelemetryDataAsset : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	// ---------------------------------------------------------------------------------------------
	// Heatmap
	// ---------------------------------------------------------------------------------------------

	/** Side length (in unreal units) of a square XY heatmap bucket. Defensive floor 1uu at read. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Heatmap", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float HeatmapBucketSizeUU = 500.f;

	/**
	 * Hard cap on the number of distinct buckets retained per category. When exceeded the lowest-
	 * weight buckets are not added (bounded memory). Defensive floor 1 at read.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Heatmap", meta = (ClampMin = "1", UIMin = "1"))
	int32 HeatmapMaxBuckets = 8192;

	// ---------------------------------------------------------------------------------------------
	// Performance
	// ---------------------------------------------------------------------------------------------

	/** Seconds between performance ticker samples. <= 0 disables the ticker (no auto-sampling). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Performance", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float PerfSampleIntervalSeconds = 1.f;

	/** Seconds between automatic Perf.Sample summary emissions. <= 0 disables auto-emit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Performance", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float PerfEmitIntervalSeconds = 30.f;

	/** A frame whose game-thread time exceeds this many milliseconds counts as a hitch. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Performance", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float HitchThresholdMs = 100.f;

	/** Number of recent frame-time samples retained for percentile computation. Floor 1 at read. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Performance", meta = (ClampMin = "1", UIMin = "1"))
	int32 PerfRingSize = 512;

	/**
	 * Percentiles (in [0,1], strictly increasing) reported in each Perf.Sample summary. Empty or
	 * non-monotonic lists are flagged by IsDataValid; a defensive p50/p90/p99 set is substituted at
	 * read time so the summary is never empty.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Performance", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	TArray<float> PerfPercentiles = { 0.5f, 0.9f, 0.99f };

	// ---------------------------------------------------------------------------------------------
	// Breadcrumb / dashboard
	// ---------------------------------------------------------------------------------------------

	/** Fixed size of the crash/error breadcrumb ring buffer. Floor 1 at read. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Breadcrumb", meta = (ClampMin = "1", UIMin = "1"))
	int32 BreadcrumbRingSize = 64;

	/** Number of most-recent events the debug dashboard retains for display. Floor 1 at read. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dashboard", meta = (ClampMin = "1", UIMin = "1"))
	int32 DashboardRecentEventCount = 32;

	/**
	 * Minimum seconds between throttled dashboard OnDashboardUpdated broadcasts. <= 0 means "every
	 * change broadcasts" (no throttle). Keeps a busy event stream from spamming the UI.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dashboard", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float DashboardThrottleSeconds = 0.25f;

	// ---------------------------------------------------------------------------------------------
	// Funnel / cohort
	// ---------------------------------------------------------------------------------------------

	/**
	 * Seconds a funnel run may stay idle before it is treated as abandoned and auto-summarised.
	 * <= 0 disables the timeout (a funnel is then only summarised on explicit Complete or shutdown).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Funnel", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float FunnelTimeoutSeconds = 0.f;

	/**
	 * Number of coarse, non-reversible cohort buckets recorded alongside a funnel summary (mirrors
	 * the experiment subsystem's player-cohort guard). Floor 2 at read so bucketing is meaningful.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Funnel", meta = (ClampMin = "2", UIMin = "2"))
	int32 FunnelCohortBucketCount = 16;

	// ---------------------------------------------------------------------------------------------
	// Validated accessors (apply defensive floors regardless of config edits)
	// ---------------------------------------------------------------------------------------------

	/** Heatmap bucket size, floored to >= 1uu so it can never divide to infinity. */
	float GetEffectiveBucketSizeUU() const { return FMath::Max(1.f, HeatmapBucketSizeUU); }

	/** Heatmap max buckets, floored to >= 1. */
	int32 GetEffectiveHeatmapMaxBuckets() const { return FMath::Max(1, HeatmapMaxBuckets); }

	/** Performance sample interval, non-negative. <= 0 means "no ticker". */
	float GetEffectivePerfSampleInterval() const { return FMath::Max(0.f, PerfSampleIntervalSeconds); }

	/** Performance emit interval, non-negative. <= 0 means "no auto-emit". */
	float GetEffectivePerfEmitInterval() const { return FMath::Max(0.f, PerfEmitIntervalSeconds); }

	/** Hitch threshold in ms, floored to >= 1ms. */
	float GetEffectiveHitchThresholdMs() const { return FMath::Max(1.f, HitchThresholdMs); }

	/** Performance ring size, floored to >= 1. */
	int32 GetEffectivePerfRingSize() const { return FMath::Max(1, PerfRingSize); }

	/** Breadcrumb ring size, floored to >= 1. */
	int32 GetEffectiveBreadcrumbRingSize() const { return FMath::Max(1, BreadcrumbRingSize); }

	/** Dashboard recent-event count, floored to >= 1. */
	int32 GetEffectiveDashboardRecentEventCount() const { return FMath::Max(1, DashboardRecentEventCount); }

	/** Dashboard throttle seconds, non-negative. */
	float GetEffectiveDashboardThrottleSeconds() const { return FMath::Max(0.f, DashboardThrottleSeconds); }

	/** Funnel timeout seconds, non-negative. <= 0 means "no timeout". */
	float GetEffectiveFunnelTimeoutSeconds() const { return FMath::Max(0.f, FunnelTimeoutSeconds); }

	/** Funnel cohort bucket count, floored to >= 2. */
	int32 GetEffectiveFunnelCohortBucketCount() const { return FMath::Max(2, FunnelCohortBucketCount); }

	/**
	 * Sanitised, strictly-increasing percentile list. If the configured list is empty or not
	 * strictly increasing within [0,1], a defensive p50/p90/p99 set is returned instead, so the
	 * performance summary is always well-formed regardless of bad config.
	 */
	TArray<float> GetEffectivePerfPercentiles() const;

#if WITH_EDITOR
	//~ Begin UObject (editor validation)
	/** Flags zero bucket size, empty / non-monotonic percentile lists, and zero-sized rings. */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
	//~ End UObject
#endif

// .generated.h MUST be the last include (UnrealHeaderTool requirement).
#include "Analytics_TelemetryDataAsset.generated.h"
};
