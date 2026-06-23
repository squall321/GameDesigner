// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "Containers/Ticker.h"
#include "Analytics_PerformanceSubsystem.generated.h"

class UAnalytics_Subsystem;
class UAnalytics_TelemetryDataAsset;

/**
 * GameInstance-scoped performance telemetry subsystem.
 *
 * An FTSTicker (interval from the telemetry data asset) samples FApp::GetDeltaTime each tick,
 * detects hitches above the data-asset HitchThresholdMs, and samples FPlatformMemory::GetStats. A
 * bounded ring (size from the data asset) of recent frame times feeds percentile computation
 * (percentile set from the data asset); the subsystem periodically emits an
 * Analytics.Event.Perf.Sample (percentile frame times + hitch count + used physical MB) through the
 * consent-gated core subsystem, plus an Analytics.Event.Perf.Hitch per detected hitch.
 *
 * It WRAPS engine stats only (FApp / FPlatformMemory); it never reinvents a profiler. The FTSTicker
 * handle is stored and removed in Deinitialize exactly like UAnalytics_Subsystem::FlushTickerHandle
 * (HARD RULE 3: every ticker registered is removed on teardown).
 *
 * All state is local/per-machine and transient; nothing replicates or saves. Recording funnels
 * through the consent-gated core subsystem, so with consent OFF the subsystem still samples locally
 * but emits nothing.
 */
UCLASS()
class DESIGNPATTERNSANALYTICS_API UAnalytics_PerformanceSubsystem : public UDP_GameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/** Take one frame-time + memory sample now (also driven automatically by the ticker). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Analytics|Performance")
	void SampleNow();

	/**
	 * Percentile frame time in milliseconds over the current ring (Percentile01 in [0,1]). Returns 0
	 * when the ring is empty. Uses nearest-rank on the sorted ring.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Analytics|Performance")
	float GetPercentileFrameMs(float Percentile01) const;

	/** Number of hitches detected since the last summary emission (running window). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Analytics|Performance")
	int32 GetHitchCount() const { return HitchCountSinceEmit; }

	/**
	 * Emit a Perf.Sample summary now (percentiles + hitch count + memory) and reset the hitch
	 * counter. Called automatically on the emit interval; exposed for explicit checkpoints.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Analytics|Performance")
	void EmitPerfSummary(FGameplayTag Reason);

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

private:
	/** Bounded ring of recent frame times (ms). Oldest overwritten once full. */
	TArray<float> FrameMsRing;

	/** Write cursor into FrameMsRing while filling / wrapping. */
	int32 RingCursor = 0;

	/** True once the ring has wrapped (so it is fully populated). */
	bool bRingFull = false;

	/** Hitches detected since the last summary emission. */
	int32 HitchCountSinceEmit = 0;

	/** Seconds accumulated toward the next auto-emit (driven by the sample ticker). */
	float TimeSinceEmit = 0.f;

	/** Last sampled used-physical memory in MB (for the summary). */
	double LastUsedPhysicalMB = 0.0;

	/** FTSTicker handle driving periodic sampling. Removed on Deinitialize. */
	FTSTicker::FDelegateHandle SampleTickerHandle;

	/** Weakly-cached core analytics subsystem (re-resolved if it goes away). */
	UPROPERTY(Transient)
	TWeakObjectPtr<UAnalytics_Subsystem> CachedAnalyticsSubsystem;

	/** Lazily-resolved telemetry data asset (weak; owned by the asset manager). */
	UPROPERTY(Transient)
	TWeakObjectPtr<const UAnalytics_TelemetryDataAsset> CachedDataAsset;

	/** True once we have attempted to resolve the data asset. */
	bool bDataAssetResolutionAttempted = false;

	/** Resolve (re-resolve) the consent-gated core analytics subsystem. May return null. */
	UAnalytics_Subsystem* ResolveAnalyticsSubsystem();

	/** Resolve (and cache) the telemetry data asset, or null. */
	const UAnalytics_TelemetryDataAsset* ResolveDataAsset();

	/** Push one frame-time sample into the ring (resizing the ring if config changed). */
	void PushFrameSample(float FrameMs, int32 RingSize);

	/** Compute the nearest-rank percentile over a SORTED copy of the ring. */
	static float PercentileOfSorted(const TArray<float>& Sorted, float Percentile01);

	/** FTSTicker callback: sample + maybe auto-emit. */
	bool TickSample(float DeltaTime);
};
