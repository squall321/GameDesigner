// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "Containers/Ticker.h"
#include "Analytics_FunnelSubsystem.generated.h"

class UAnalytics_Subsystem;
class UAnalytics_TelemetryDataAsset;

/**
 * Live state of one in-flight named funnel run (GI-scoped, local, transient).
 *
 * A funnel here is an ad-hoc ORDERED progression keyed by a funnel id tag, complementing — not
 * replacing — the per-actor UAnalytics_ProgressionComponent (which tracks a data-asset funnel on
 * one actor). This subsystem tracks GI-wide named funnels plus session cohort dimensions, computes
 * consecutive-step drop-off, and emits a single summary through the consent-gated core subsystem.
 */
USTRUCT()
struct FAnalytics_FunnelRun
{
	GENERATED_BODY()

	/** Ordered list of distinct steps reached so far (in the order AdvanceFunnelStep first saw them). */
	UPROPERTY()
	TArray<FGameplayTag> StepsReached;

	/** FApp time the run began (for timeout / duration). */
	UPROPERTY()
	double BeginTimeSeconds = 0.0;

	/** FApp time of the most recent advance (idle-timeout reference). */
	UPROPERTY()
	double LastAdvanceTimeSeconds = 0.0;

	/** True once a summary has been emitted (so timeout + Complete don't double-emit). */
	UPROPERTY()
	bool bSummarised = false;

	/** Number of steps reached (== funnel depth). */
	int32 Depth() const { return StepsReached.Num(); }
};

/**
 * GameInstance-wide named multi-step funnel + cohort analysis subsystem.
 *
 * Responsibilities:
 *  - Tracks ordered step progress per named funnel id; AdvanceFunnelStep records the first time
 *    each step is reached and records an Analytics.Event.Funnel.Step event.
 *  - Tags the session into FGameplayTag cohort dimensions supplied by the host (SetCohortTag) — or,
 *    when present, by the optional telemetry-context seam — and folds those into every summary.
 *  - On CompleteFunnel / idle timeout, emits Analytics.Event.Funnel.Summary carrying deepest
 *    ordinal, completed flag, duration, the active cohort tags, and a coarse player bucket obtained
 *    from UAnalytics_Subsystem::GetExperimentBucket (the CORE subsystem — NOT the experiment
 *    subsystem), so a privacy-safe cohort balance check is possible without recording any id.
 *
 * GC / lifetime: holds the core subsystem WEAKLY and re-resolves on use (a GI subsystem must never
 * keep a hard cross-system ref that could outlive a re-created sibling). All state is local and
 * transient; nothing replicates or saves. The optional idle-timeout sweep uses an FTSTicker whose
 * handle is removed in Deinitialize.
 *
 * Consent: every emission funnels through the consent-gated core RecordEvent, so with consent OFF
 * the subsystem still tracks depth locally but emits nothing.
 */
UCLASS()
class DESIGNPATTERNSANALYTICS_API UAnalytics_FunnelSubsystem : public UDP_GameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/**
	 * Begin (or restart) a named funnel run. Records Analytics.Event.Funnel.Begin. Restarting an
	 * in-flight run first summarises the previous run as abandoned, so no progress is silently lost.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Analytics|Funnel")
	void BeginFunnel(FGameplayTag FunnelId);

	/**
	 * Advance the named funnel by reaching StepTag. Idempotent per (funnel, step): the first time a
	 * step is reached it is appended and a Funnel.Step event recorded; repeats are ignored. A step on
	 * a funnel that was never begun implicitly begins it (so callers can fire steps directly).
	 *
	 * @return True if this call recorded a NEW step.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Analytics|Funnel")
	bool AdvanceFunnelStep(FGameplayTag FunnelId, FGameplayTag StepTag);

	/** Complete a named funnel: emits the Funnel.Summary (completed = true) and clears the run. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Analytics|Funnel")
	void CompleteFunnel(FGameplayTag FunnelId);

	/**
	 * Set a session cohort dimension -> value. Cohort values are folded into every funnel summary as
	 * Tag attributes (privacy-safe; FGameplayTag only). Host-pushed (e.g. install-week, acquisition
	 * source). Overwrites a previous value for the same dimension.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Analytics|Funnel")
	void SetCohortTag(FGameplayTag CohortDimension, FGameplayTag CohortValue);

	/** Current value for a cohort dimension, or an empty tag if unset. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Analytics|Funnel")
	FGameplayTag GetCohortTag(FGameplayTag CohortDimension) const;

	/** Deepest step count reached for a named funnel (0 if unknown / never begun). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Analytics|Funnel")
	int32 GetFunnelDepth(FGameplayTag FunnelId) const;

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

private:
	/** All in-flight funnel runs, keyed by funnel id. */
	UPROPERTY(Transient)
	TMap<FGameplayTag, FAnalytics_FunnelRun> Runs;

	/** Session cohort dimensions -> values, folded into every summary. */
	UPROPERTY(Transient)
	TMap<FGameplayTag, FGameplayTag> CohortTags;

	/** Weakly-cached core analytics subsystem (GI-scoped; re-resolved if it goes away). */
	UPROPERTY(Transient)
	TWeakObjectPtr<UAnalytics_Subsystem> CachedAnalyticsSubsystem;

	/** Lazily-resolved telemetry data asset (owned by the asset manager; weak). */
	UPROPERTY(Transient)
	TWeakObjectPtr<const UAnalytics_TelemetryDataAsset> CachedDataAsset;

	/** True once we have attempted to resolve the data asset (so a missing asset isn't retried). */
	bool bDataAssetResolutionAttempted = false;

	/** FTSTicker driving the idle-timeout sweep; removed on Deinitialize. Inactive when timeout <= 0. */
	FTSTicker::FDelegateHandle TimeoutTickerHandle;

	/** Resolve (re-resolve) the consent-gated core analytics subsystem from this GI. May return null. */
	UAnalytics_Subsystem* ResolveAnalyticsSubsystem();

	/** Resolve (and cache) the telemetry data asset, or null. */
	const UAnalytics_TelemetryDataAsset* ResolveDataAsset();

	/** Emit the Funnel.Summary event for a run (idempotent via bSummarised). */
	void SummariseRun(FGameplayTag FunnelId, FAnalytics_FunnelRun& Run, bool bCompleted);

	/** FTSTicker callback: summarise idle runs that exceeded the configured timeout. */
	bool TickTimeoutSweep(float DeltaTime);
};
