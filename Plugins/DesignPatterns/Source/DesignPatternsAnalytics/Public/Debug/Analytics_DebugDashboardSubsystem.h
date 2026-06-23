// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "Containers/Ticker.h"
#include "Analytics/Seam_AnalyticsSink.h"
#include "Crash/Analytics_BreadcrumbSubsystem.h" // for FAnalytics_Breadcrumb (recent-event view)
#include "Analytics_DebugDashboardSubsystem.generated.h"

class UAnalytics_Subsystem;
class UAnalytics_TelemetryDataAsset;

/**
 * A throttled, copyable snapshot of the live debug dashboard state.
 *
 * PLAIN value (FGameplayTag / int / FAnalytics_Breadcrumb only; no UObject refs), so it can be
 * passed by const ref to observers and copied freely. Reuses FAnalytics_Breadcrumb as the
 * recent-event view type (tag + timestamp + PII-safe attrs).
 */
USTRUCT(BlueprintType)
struct FAnalytics_DashboardSnapshot
{
	GENERATED_BODY()

	/** Rolling count of recorded events, keyed by event tag. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Analytics|Dashboard")
	TMap<FGameplayTag, int32> RollingCounts;

	/** The most-recent N recorded events (newest last), capped by DashboardRecentEventCount. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Analytics|Dashboard")
	TArray<FAnalytics_Breadcrumb> Recent;

	/** Total events observed this session (monotonic). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Analytics|Dashboard")
	int32 TotalObserved = 0;
};

/**
 * Broadcast when the throttled dashboard snapshot updates. Non-dynamic multicast: the consumer is a
 * host/UI widget that binds in C++; this module adds NO UMG/Slate dependency. Payload is a const ref
 * to a plain copyable snapshot.
 */
DECLARE_MULTICAST_DELEGATE_OneParam(FAnalytics_OnDashboardUpdated, const FAnalytics_DashboardSnapshot& /*Snapshot*/);

/**
 * GameInstance-scoped live QA stats feed — OFF by default in a shipping posture.
 *
 * Observes recorded telemetry via the core subsystem's additive OnEventRecorded delegate (so it sees
 * exactly what was recorded, post-consent) and aggregates a rolling per-tag count plus a recent-event
 * ring. It broadcasts a THROTTLED OnDashboardUpdated (cadence from the telemetry data asset) carrying
 * a copyable snapshot. The actual widget lives in the host/UI module and binds via this delegate —
 * this module stays genre-agnostic and UMG-free.
 *
 * Read-only sibling: it NEVER changes core recording. It unbinds its OnEventRecorded handler and
 * removes its throttle ticker in Deinitialize (HARD RULE 3). All state is local/per-machine and
 * transient.
 */
UCLASS()
class DESIGNPATTERNSANALYTICS_API UAnalytics_DebugDashboardSubsystem : public UDP_GameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/** Bind a C++ consumer (e.g. a host QA widget) to throttled snapshot updates. */
	FAnalytics_OnDashboardUpdated OnDashboardUpdated;

	/** A copy of the current dashboard snapshot. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Analytics|Dashboard")
	FAnalytics_DashboardSnapshot GetSnapshot() const;

	/** Rolling count for a specific event tag (0 if unseen). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Analytics|Dashboard")
	int32 GetEventCount(FGameplayTag EventTag) const;

	/**
	 * Enable/disable live aggregation at runtime. When disabled the subsystem unbinds its observer
	 * and stops broadcasting (a QA toggle). Re-enabling re-binds. Does nothing if the master settings
	 * switch (bEnableDebugDashboard) is off.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Analytics|Dashboard")
	void SetEnabled(bool bEnabled);

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

private:
	/** Rolling per-tag counts. */
	UPROPERTY(Transient)
	TMap<FGameplayTag, int32> RollingCounts;

	/** Recent-event ring (newest last). */
	UPROPERTY(Transient)
	TArray<FAnalytics_Breadcrumb> Recent;

	/** Total events observed this session. */
	int32 TotalObserved = 0;

	/** True while actively aggregating (gated by the settings master switch + SetEnabled). */
	bool bActive = false;

	/** True when there is an un-broadcast change pending (drives the throttle). */
	bool bDirty = false;

	/** Weakly-cached core analytics subsystem (re-resolved if it goes away). */
	UPROPERTY(Transient)
	TWeakObjectPtr<UAnalytics_Subsystem> CachedAnalyticsSubsystem;

	/** Lazily-resolved telemetry data asset (weak; owned by the asset manager). */
	UPROPERTY(Transient)
	TWeakObjectPtr<const UAnalytics_TelemetryDataAsset> CachedDataAsset;

	/** True once we have attempted to resolve the data asset. */
	bool bDataAssetResolutionAttempted = false;

	/** Our subscription to the core subsystem's OnEventRecorded delegate; removed on disable/teardown. */
	FDelegateHandle EventRecordedHandle;

	/** FTSTicker driving the throttled broadcast; removed on disable/teardown. */
	FTSTicker::FDelegateHandle ThrottleTickerHandle;

	/** Resolve (re-resolve) the consent-gated core analytics subsystem. May return null. */
	UAnalytics_Subsystem* ResolveAnalyticsSubsystem();

	/** Resolve (and cache) the telemetry data asset, or null. */
	const UAnalytics_TelemetryDataAsset* ResolveDataAsset();

	/** Bind OnEventRecorded + start the throttle ticker. */
	void BeginAggregation();

	/** Unbind OnEventRecorded + stop the throttle ticker. */
	void EndAggregation();

	/** OnEventRecorded handler: fold the event into the aggregate (marks dirty). */
	void HandleEventRecorded(FGameplayTag EventTag, const TArray<FSeam_AnalyticsAttr>& Attributes);

	/** Build a snapshot from the current aggregate. */
	FAnalytics_DashboardSnapshot BuildSnapshot() const;

	/** FTSTicker callback: broadcast a snapshot when dirty (throttled cadence). */
	bool TickThrottle(float DeltaTime);
};
