// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "Containers/Ticker.h"
#include "Lvl_MemoryBudgetWatcherSubsystem.generated.h"

class ULvl_StreamingProfileDataAsset;

/**
 * PER-MACHINE memory-budget observer for the level-director streaming system.
 *
 * On an FTSTicker cadence (interval from the profile data asset) it estimates the resident streaming
 * footprint — preferring the platform's physical-memory delta when available, else a data-authored
 * per-resident-level proxy times the world's resident ULevelStreaming count — and compares it to the
 * profile's MemoryBudgetMB. When over budget it raises a PER-MACHINE pressure scalar (0 = none .. 1 =
 * saturated) that prefetch components read to SHRINK their extra interest radius at the source, letting
 * the existing streaming director unload naturally.
 *
 * It deliberately does NOT command ISeam_StreamingControl: that seam has no per-category eviction and
 * LevelDirector is its declared PRODUCER, so consuming it here would be circular. Per-category pressure
 * is derived from the global pressure weighted by the profile's CategoryPriority (lower priority feels
 * pressure sooner).
 *
 * NON-REPLICATED, NON-SAVED: every machine watches its own memory. The HasWorldAuthority accessor exists
 * only for symmetry with the other world subsystems (rule 4) — pressure decisions never gate on it.
 */
UCLASS()
class DESIGNPATTERNSLEVELDIRECTOR_API ULvl_MemoryBudgetWatcherSubsystem : public UDP_WorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/** True on server / standalone / listen-server host. Per-machine; pressure never gates on this. */
	bool HasWorldAuthority() const
	{
		const UWorld* W = GetWorld();
		return W && W->GetNetMode() != NM_Client;
	}

	/**
	 * Streaming profile supplying the budget, cadence and category priorities. When null the watcher
	 * uses documented defensive fallbacks (a large budget, a fixed cadence) so it never divides by zero.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|Streaming")
	TObjectPtr<ULvl_StreamingProfileDataAsset> Profile;

	/**
	 * Current per-category memory pressure in [0,1] (0 = no pressure, 1 = clamp this category fully).
	 * A category absent from the profile uses the default priority. An invalid Category returns the
	 * global pressure unmodified.
	 */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Lvl|Streaming")
	float GetCategoryPressure(FGameplayTag Category) const;

	/** Current global pressure in [0,1] (overshoot fraction over budget, saturated by the profile). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Lvl|Streaming")
	float GetGlobalPressure() const { return GlobalPressure; }

	/** Last estimated resident footprint (MB). Diagnostic. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Lvl|Streaming")
	float GetLastEstimatedResidentMB() const { return LastEstimatedResidentMB; }

	/** Force an immediate budget evaluation (e.g. right after a large streaming change). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Lvl|Streaming")
	void EvaluateBudget();

	//~ Begin UDP_WorldSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_WorldSubsystem

private:
	/** FTSTicker callback: advances the cadence accumulator and runs EvaluateBudget when due. */
	bool Tick(float DeltaTime);

	/** Estimate the resident streaming footprint (MB) for the current world. */
	float EstimateResidentMB() const;

	/** The effective profile, or null (callers use defensive fallbacks). */
	const ULvl_StreamingProfileDataAsset* GetProfile() const { return Profile; }

	/** Effective budget (MB) — profile value or a large defensive fallback. */
	float GetEffectiveBudgetMB() const;

	/** Effective cadence (s) — profile value or a defensive fallback, never below a safe minimum. */
	float GetEffectiveInterval() const;

	/** Tick handle for the cadence-driven evaluation loop. Removed in Deinitialize. */
	FTSTicker::FDelegateHandle TickerHandle;

	/** Seconds accumulated toward the next evaluation. */
	float TimeSinceLastEvaluation = 0.f;

	/** Current global pressure [0,1], updated each EvaluateBudget. */
	float GlobalPressure = 0.f;

	/** Last estimated resident footprint (MB). */
	float LastEstimatedResidentMB = 0.f;

	/** Baseline physical-memory-used (MB) captured at Initialize, for the delta-based estimate. */
	float BaselinePhysicalMB = 0.f;

	/** True if BaselinePhysicalMB is meaningful (platform exposed a used-physical stat). */
	bool bHasBaseline = false;
};
