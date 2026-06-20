// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Analytics_ProgressionComponent.generated.h"

class UAnalytics_Subsystem;
class UAnalytics_ProgressionDataAsset;

/**
 * Fired when a milestone funnel step is reached for the first time.
 * @param MilestoneTag The step tag that was reached.
 * @param FunnelDepthReached 1-based ordinal of the deepest funnel step reached so far.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FAnalytics_OnMilestoneReached,
	FGameplayTag, MilestoneTag, int32, FunnelDepthReached);

/**
 * Per-actor progression/session telemetry component.
 *
 * What it measures (all LOCAL/per-machine; nothing replicates or saves into gameplay state):
 *  - Playtime accumulation: while the component is active it accumulates wall-clock playtime and
 *    emits a periodic heartbeat event (interval from the funnel data asset). Playtime pauses when
 *    the owner is torn down or the component is deactivated.
 *  - Funnel progress: RecordFunnelStep(StepTag) records the FIRST time each step is reached and
 *    tracks the deepest step index reached (the "funnel depth"). Steps and which of them are
 *    milestones are defined entirely in the funnel data asset (no magic step names in code).
 *  - Milestones: when a reached step is flagged as a milestone in the asset, OnMilestoneReached
 *    fires and a milestone analytics event is recorded.
 *
 * All recording flows through the consent-gated UAnalytics_Subsystem (core area), so with consent
 * OFF the component still tracks depth/playtime locally but emits no telemetry. If the analytics
 * subsystem is unavailable the component is inert for recording but still fires OnMilestoneReached
 * for gameplay use (the delegate is a gameplay signal, not telemetry).
 *
 * Attach this to the player controller / player state / pawn whose progression you want to measure.
 */
UCLASS(ClassGroup = (DesignPatterns), meta = (BlueprintSpawnableComponent),
	HideCategories = (Variable, Sockets, Tags, ComponentReplication, Activation, Cooking, AssetUserData, Collision))
class DESIGNPATTERNSANALYTICS_API UAnalytics_ProgressionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAnalytics_ProgressionComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent

	/** Fires the first time a milestone-flagged funnel step is reached. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Analytics|Progression")
	FAnalytics_OnMilestoneReached OnMilestoneReached;

	/**
	 * Identity tag of the funnel data asset (UAnalytics_ProgressionDataAsset) this component tracks
	 * against. Resolved through the data registry lazily on first need. When unset or unresolved the
	 * component records funnel steps as flat events (no depth/milestone semantics) — a documented
	 * inert fallback, not a crash.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Analytics|Progression")
	FGameplayTag FunnelDefinitionTag;

	/**
	 * If true, the component accumulates playtime and emits heartbeats. Disable on actors where
	 * playtime is meaningless (e.g. a spectator). Defaults true.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Analytics|Progression")
	bool bTrackPlaytime = true;

	/**
	 * Record that a funnel step was reached.
	 *
	 * Idempotent per step: the FIRST call for a given StepTag records a funnel-step event (and a
	 * milestone event + OnMilestoneReached if the asset flags it as a milestone) and updates the
	 * deepest-step tracking; subsequent calls for the same step are ignored. A step not present in
	 * the funnel asset is still recorded as a flat funnel-step event (depth attribute omitted).
	 *
	 * @return True if this call recorded a NEW step (false if it was already reached).
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Analytics|Progression")
	bool RecordFunnelStep(FGameplayTag StepTag);

	/** True if StepTag has already been reached this session. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Analytics|Progression")
	bool HasReachedStep(FGameplayTag StepTag) const { return ReachedSteps.Contains(StepTag); }

	/** Deepest 1-based funnel ordinal reached so far (0 if none / no funnel asset). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Analytics|Progression")
	int32 GetFunnelDepthReached() const { return DeepestStepIndexReached + 1; }

	/** Accumulated active playtime in seconds for this component's lifetime. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Analytics|Progression")
	float GetAccumulatedPlaytimeSeconds() const { return AccumulatedPlaytimeSeconds; }

	/** Force a playtime-heartbeat event now (also resets the heartbeat timer). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Analytics|Progression")
	void FlushPlaytimeHeartbeat();

protected:
	/** Resolve (and cache) the funnel data asset from the data registry. May return null. */
	const UAnalytics_ProgressionDataAsset* ResolveFunnelAsset();

	/** Resolve the consent-gated core analytics subsystem from this actor's GameInstance. */
	UAnalytics_Subsystem* ResolveAnalyticsSubsystem();

	/** Emit a heartbeat event carrying accumulated playtime and current funnel depth. */
	void EmitPlaytimeHeartbeat();

private:
	/** Steps reached this session (dedup set for idempotent RecordFunnelStep). */
	UPROPERTY(Transient)
	TSet<FGameplayTag> ReachedSteps;

	/** Deepest funnel step INDEX reached (INDEX_NONE until the first known step). */
	int32 DeepestStepIndexReached = INDEX_NONE;

	/** Active playtime accumulated since BeginPlay (seconds). */
	float AccumulatedPlaytimeSeconds = 0.0f;

	/** Seconds since the last heartbeat emission. */
	float TimeSinceHeartbeat = 0.0f;

	/** Lazily-resolved funnel asset. Weak: it is owned by the asset manager, not by this component. */
	UPROPERTY(Transient)
	TWeakObjectPtr<const UAnalytics_ProgressionDataAsset> CachedFunnelAsset;

	/** True once we have attempted to resolve the funnel asset (so a missing asset isn't retried). */
	bool bFunnelResolutionAttempted = false;

	/** Weakly-cached core analytics subsystem (GI-scoped; re-resolved if it goes away). */
	UPROPERTY(Transient)
	TWeakObjectPtr<UAnalytics_Subsystem> CachedAnalyticsSubsystem;
};
