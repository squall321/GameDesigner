// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UObject/ScriptInterface.h"
#include "GameplayTagContainer.h"
#include "SimEco_ProcessComponentBase.generated.h"

class USimEco_StockpileComponent;
class USimEco_ProcessDef;
class ISeam_SimClock;

/**
 * Shared, authority-driven base for the producer and consumer process components.
 *
 * Both run a recipe (USimEco_ProcessDef) in repeating fixed cycles against a target stockpile,
 * advancing an accumulator in *scaled simulation time* (RealDelta * clock time-scale, skipped while
 * the clock is paused). To keep the wire cheap, only three things replicate:
 *   - the active process tag,
 *   - the server world-time at which the current cycle started (ServerCycleStartTime),
 *   - the cycle length (ActiveCycleSeconds).
 * Clients derive smooth progress from those without per-frame replication. Authoritative cycle
 * completion (reserve/commit/produce) happens only on the server.
 *
 * Subclasses implement the actual per-cycle effect (OnCycleBegun / OnCycleCompleted) and the
 * relevant seam (producer or consumer). Every state mutator guards authority at the top.
 */
UCLASS(Abstract, ClassGroup = (DesignPatternsSimEconomy))
class DESIGNPATTERNSSIMECONOMY_API USimEco_ProcessComponentBase : public UActorComponent
{
	GENERATED_BODY()

public:
	USimEco_ProcessComponentBase();

	//~ Begin UActorComponent
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent

	/**
	 * Begin running ProcessTag in repeating cycles. AUTHORITY ONLY (no-op on clients). Cancels any
	 * current process first (releasing its reservations). Returns true if the process was accepted
	 * (valid recipe, facility satisfied).
	 */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Process")
	bool StartProcess(FGameplayTag ProcessTag);

	/**
	 * Stop the active process, releasing any reserved-but-uncommitted inputs back to the stockpile.
	 * AUTHORITY ONLY.
	 */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Process")
	void CancelProcess();

	/** Tag of the running process (invalid when idle). Client-safe (replicated). */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Process")
	FGameplayTag GetProcessTag() const { return ActiveProcessTag; }

	/** True while a process is active. */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Process")
	bool IsRunning() const { return ActiveProcessTag.IsValid(); }

	/**
	 * Normalized progress [0,1] through the current cycle, derived from the replicated cycle start
	 * time and length. Smooth and correct on clients without ticking authoritative state.
	 */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Process")
	float GetCycleProgress() const;

	/** The stockpile this component produces into / consumes from (resolved lazily off the owner). */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Process")
	USimEco_StockpileComponent* GetTargetStockpile() const;

	/** This site's facility-type tag, checked against a process's RequiredFacilityTag. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimEconomy|Process")
	FGameplayTag FacilityTag;

	/**
	 * Optional explicit stockpile override. When unset the component uses the first
	 * USimEco_StockpileComponent found on its owning actor.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimEconomy|Process")
	TObjectPtr<USimEco_StockpileComponent> StockpileOverride = nullptr;

	/**
	 * Optional explicit sim-clock provider. When unset the component resolves the shared clock seam
	 * from the service locator (gated by USimEco_DeveloperSettings::bAutoBindSurvivalClock).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimEconomy|Process")
	TScriptInterface<ISeam_SimClock> ClockOverride;

protected:
	/** True if the owning actor has network authority. */
	bool HasAuthority() const;

	/** Resolve the active process definition from the core data registry (null when idle/unknown). */
	const USimEco_ProcessDef* ResolveProcessDef() const;

	/** Resolve the effective sim clock (override or service-locator seam). May be null. */
	TScriptInterface<ISeam_SimClock> ResolveClock() const;

	/** Current server world-time in seconds (authoritative timeline used for cycle start stamps). */
	double GetServerTimeSeconds() const;

	/**
	 * SERVER: called when a fresh cycle begins. Subclass reserves inputs here. Return false to
	 * stall the cycle (e.g. insufficient inputs) — the base will retry next tick without consuming.
	 */
	virtual bool OnCycleBegun(const USimEco_ProcessDef& Def, USimEco_StockpileComponent& Stockpile) { return true; }

	/**
	 * SERVER: called when a cycle completes. Subclass commits reserved inputs and adds outputs here.
	 */
	virtual void OnCycleCompleted(const USimEco_ProcessDef& Def, USimEco_StockpileComponent& Stockpile) {}

	/**
	 * SERVER: called when a process is cancelled (or replaced). Subclass releases any reservations.
	 */
	virtual void OnProcessCancelled(const USimEco_ProcessDef& Def, USimEco_StockpileComponent& Stockpile) {}

	/** Broadcast a tick-completed bus event after a server cycle commit (subclasses may extend). */
	void BroadcastTickCompleted();

	/** OnRep for the replicated process tag — lets clients react to start/stop. */
	UFUNCTION()
	void OnRep_ActiveProcessTag();

	/** Replicated identity of the running process (invalid when idle). */
	UPROPERTY(ReplicatedUsing = OnRep_ActiveProcessTag)
	FGameplayTag ActiveProcessTag;

	/**
	 * Replicated server world-time (seconds) at which the current cycle started. Clients derive
	 * progress as (Now - ServerCycleStartTime) / ActiveCycleSeconds, clamped to [0,1].
	 */
	UPROPERTY(Replicated)
	double ServerCycleStartTime = 0.0;

	/** Replicated length of the current cycle in scaled sim-seconds. */
	UPROPERTY(Replicated)
	double ActiveCycleSeconds = 0.0;

	/**
	 * SERVER-ONLY accumulator of scaled sim-time elapsed in the current cycle. Not replicated;
	 * clients use the start-time stamp instead.
	 */
	double ServerCycleAccumulator = 0.0;

	/**
	 * SERVER-ONLY: true once the current cycle's inputs have been successfully reserved (OnCycleBegun
	 * returned true). While false the base keeps retrying OnCycleBegun and does not advance time.
	 */
	bool bServerCycleReserved = false;

private:
	/** SERVER: advance the active cycle by ScaledDelta sim-seconds, completing/restarting as needed. */
	void ServerAdvance(double ScaledDelta);

	/** SERVER: (re)stamp the replicated cycle-start fields for a new cycle starting now. */
	void ServerBeginNewCycle(const USimEco_ProcessDef& Def);
};
