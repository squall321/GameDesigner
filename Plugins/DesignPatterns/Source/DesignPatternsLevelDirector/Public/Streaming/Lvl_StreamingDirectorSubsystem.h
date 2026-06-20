// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "UObject/ScriptInterface.h"
#include "UObject/WeakInterfacePtr.h"
#include "GameplayTagContainer.h"
#include "Containers/Ticker.h"
#include "Settings/Lvl_DeveloperSettings.h"
#include "Lvl_StreamingDirectorSubsystem.generated.h"

class ILvl_InterestSource;
class ISeam_ActivationGate;
class ISeam_AnalyticsSink;
class ULevelStreaming;

/**
 * Snapshot of an interest source plus its effective query bubble, computed once per evaluation pass.
 * Plain POD (no UObject refs needing GC); built transiently and discarded each pass.
 */
struct FLvl_InterestQuery
{
	/** World location the source wants content around. */
	FVector Location = FVector::ZeroVector;

	/** Extra radius (>=0) requested by the source on top of the policy bands. */
	float ExtraRadius = 0.f;
};

/**
 * Per-level streaming bookkeeping the director maintains across passes (NOT replicated, NOT saved).
 * Keyed by the streaming level object. Tracks the last decision so we honour hysteresis and budgets.
 */
USTRUCT()
struct DESIGNPATTERNSLEVELDIRECTOR_API FLvl_TrackedLevelState
{
	GENERATED_BODY()

	/** The classic streaming level this state tracks. Weak: the world owns it; we only observe. */
	UPROPERTY(Transient)
	TWeakObjectPtr<ULevelStreaming> StreamingLevel;

	/** Category tag resolved for this level (drives which distance band applies). */
	UPROPERTY(Transient)
	FGameplayTag Category;

	/** Last distance (world units) from this level to the nearest interest source. */
	UPROPERTY(Transient)
	float LastNearestDistance = TNumericLimits<float>::Max();

	/** True if the director currently WANTS this level resident (its last load decision). */
	UPROPERTY(Transient)
	bool bWantsLoaded = false;

	/** True if the director currently wants this level made visible (vs loaded-but-hidden). */
	UPROPERTY(Transient)
	bool bWantsVisible = false;
};

/**
 * World-scoped streaming director.
 *
 * Loads/unloads classic streaming sublevels (ULevelStreaming) — and, when a World Partition
 * subsystem is present, registers per-interest-source streaming sources so WP streams cells —
 * around the set of registered ILvl_InterestSource objects, using the data-driven distance-band
 * policy in ULvl_DeveloperSettings with per-frame load/unload budgets and a coarse evaluation
 * cadence. Streaming is a PER-MACHINE decision: this subsystem never replicates and never persists.
 *
 * Engine wrapping (rule 5): it wraps ULevelStreaming::SetShouldBeLoaded/SetShouldBeVisible and the
 * UWorldPartitionSubsystem streaming-source API. World Partition is resolved SOFTLY by class name so
 * the module does not hard-depend on the WorldPartition module and degrades gracefully when absent.
 *
 * Seam usage (rule 10): an ISeam_ActivationGate (resolved from the service locator, default OPEN)
 * can globally suspend streaming; an ISeam_AnalyticsSink (default no-op) receives a rate-limited
 * aggregate churn event. Both are optional; the director runs fully without either.
 *
 * Authority (rule 4): UWorldSubsystem has no HasWorldAuthority(); this subsystem declares its own.
 * Streaming itself is authority-independent (every client streams for itself), but the accessor is
 * provided for symmetry and for any future authority-gated diagnostics.
 */
UCLASS()
class DESIGNPATTERNSLEVELDIRECTOR_API ULvl_StreamingDirectorSubsystem : public UDP_WorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/**
	 * UWorldSubsystem has no HasWorldAuthority(); declare our own. True on server / standalone /
	 * listen-server host (any net mode that is not a pure client). Provided per rule 4; streaming
	 * decisions themselves are per-machine and do not gate on this.
	 */
	bool HasWorldAuthority() const
	{
		const UWorld* W = GetWorld();
		return W && W->GetNetMode() != NM_Client;
	}

	// ---- Interest source registration ----------------------------------------------------------

	/**
	 * Register an interest source the director should stream content around. The source is held
	 * WEAKLY (TWeakInterfacePtr) and null-checked on use, so destroying the source simply stops its
	 * contribution. Re-registering the same source is a no-op. Triggers a re-evaluation next pass.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|LevelDirector|Streaming")
	void RegisterInterestSource(const TScriptInterface<ILvl_InterestSource>& Source);

	/** Remove a previously-registered interest source. Returns true if one was removed. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|LevelDirector|Streaming")
	bool UnregisterInterestSource(const TScriptInterface<ILvl_InterestSource>& Source);

	/** Number of currently-registered (non-pruned) interest sources. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|LevelDirector|Streaming")
	int32 GetInterestSourceCount() const;

	// ---- Control --------------------------------------------------------------------------------

	/**
	 * Force a full re-evaluation on the next tick regardless of the cadence timer. Useful after a
	 * teleport, a level-load, or a policy change. Cheap: just clears the cadence accumulator.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|LevelDirector|Streaming")
	void RequestImmediateReevaluation();

	/**
	 * Manually suspend/resume the director independently of the activation gate. While suspended the
	 * director issues no new load/unload requests (existing resident levels are left as-is).
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|LevelDirector|Streaming")
	void SetDirectorEnabled(bool bEnabled);

	/** True if the director is currently allowed to run (manual switch AND activation gate open). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|LevelDirector|Streaming")
	bool IsDirectorActive() const;

	//~ Begin UDP_WorldSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_WorldSubsystem

protected:
	//~ Begin UWorldSubsystem (per-world tick driven via FTSTicker; we are not an FTickableGameObject
	//   to avoid editor/seamless-travel ticking, matching the message bus pattern).
	//~ End UWorldSubsystem

private:
	/** Manual enable switch (AND-ed with the activation gate to form IsDirectorActive). */
	UPROPERTY(Transient)
	bool bManuallyEnabled = true;

	/**
	 * Registered interest sources. Held WEAKLY: a world subsystem must not keep a dead pawn alive,
	 * and the contract is explicitly non-owning. Pruned each pass.
	 */
	TArray<TWeakInterfacePtr<ILvl_InterestSource>> InterestSources;

	/** Per-streaming-level bookkeeping, keyed by the level's package FName for stable lookup. */
	UPROPERTY(Transient)
	TMap<FName, FLvl_TrackedLevelState> TrackedLevels;

	/** Cached weak ref to the resolved activation gate (rule 3: weak in a long-lived holder). */
	TWeakInterfacePtr<ISeam_ActivationGate> CachedActivationGate;

	/** Cached weak ref to the resolved analytics sink. Null/stale => analytics no-op. */
	TWeakInterfacePtr<ISeam_AnalyticsSink> CachedAnalyticsSink;

	/** Tick handle for the cadence-driven evaluation loop. */
	FTSTicker::FDelegateHandle TickerHandle;

	/** Seconds accumulated toward the next evaluation pass. */
	float TimeSinceLastEvaluation = 0.f;

	/** Seconds accumulated toward the next analytics churn report. */
	float TimeSinceLastReport = 0.f;

	/** Last evaluated centroid of interest sources, for the move-threshold early re-eval. */
	FVector LastEvaluationCentroid = FVector::ZeroVector;

	/** True once at least one evaluation pass has run (so the centroid is meaningful). */
	bool bHasEvaluatedOnce = false;

	/** Round-robin cursor into the tracked-level list, so we amortize evaluation across passes. */
	int32 EvaluationCursor = 0;

	// --- Churn counters since the last analytics report (reset on report) ---
	int32 ChurnLoadsSinceReport = 0;
	int32 ChurnUnloadsSinceReport = 0;

	/** Whether we successfully registered WP streaming sources this session (resolved softly). */
	bool bWorldPartitionActive = false;

	/** Handles for WP streaming sources we registered, so we can unregister them on teardown. */
	TArray<FName> RegisteredWorldPartitionSourceNames;

	// ---- Internals ------------------------------------------------------------------------------

	/** FTSTicker callback: advances cadence timers and runs an evaluation pass when due. */
	bool Tick(float DeltaTime);

	/** Prune dead interest sources; returns the live ones' query bubbles. */
	void BuildInterestQueries(TArray<FLvl_InterestQuery>& OutQueries);

	/** Centroid of the current interest queries (for the move-threshold check). */
	static FVector ComputeCentroid(const TArray<FLvl_InterestQuery>& Queries);

	/** Run one streaming evaluation pass under the per-frame budgets. */
	void EvaluateStreaming(const TArray<FLvl_InterestQuery>& Queries);

	/** Refresh the tracked-level map from the world's current ULevelStreaming list. */
	void RefreshTrackedLevels();

	/** Resolve the distance band that applies to a level of the given category (never null result). */
	const FLvl_DistanceBand& ResolveBandForCategory(FGameplayTag Category) const;

	/** Nearest distance from a world location to any interest query (accounting for extra radius). */
	static float NearestDistanceToInterest(const FVector& LevelOrigin, const TArray<FLvl_InterestQuery>& Queries);

	/** Apply a load/visible decision to a classic streaming level, wrapping the engine API. */
	void ApplyLevelDecision(ULevelStreaming* Level, bool bShouldLoad, bool bShouldBeVisible);

	/** Register/refresh per-source World Partition streaming sources, if WP is present and enabled. */
	void UpdateWorldPartitionSources(const TArray<FLvl_InterestQuery>& Queries);

	/** Remove any World Partition streaming sources we registered (teardown / disable). */
	void ClearWorldPartitionSources();

	/** Resolve (and cache weakly) the activation gate from the service locator. */
	ISeam_ActivationGate* ResolveActivationGate();

	/** Resolve (and cache weakly) the analytics sink from the service locator. */
	ISeam_AnalyticsSink* ResolveAnalyticsSink();

	/** Emit the rate-limited aggregate churn analytics event (no-op if sink unresolved or disabled). */
	void MaybeEmitChurnAnalytics();

	/** The effective settings CDO, or nullptr (callers use documented fallbacks when null). */
	const ULvl_DeveloperSettings* GetSettings() const;
};
