// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "Clock/Seam_SimClock.h"
#include "Containers/Ticker.h"
#include "GameplayTagContainer.h"

class ISimEco_StepListener;
class USimEco_MarketSubsystem;

// FInstancedStruct lives in StructUtils on UE 5.3/5.4, merged into CoreUObject in 5.5.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

// .generated.h MUST be the last include (UnrealHeaderTool requirement).
#include "SimEco_EconomySubsystem.generated.h"

/**
 * Message-bus payload broadcast on SimEcoNativeTags::Bus_TickCompleted after each authoritative
 * fixed economy step. Lets UI/agents react to the discrete simulation cadence.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMECONOMY_API FSimEco_TickCompletedMsg
{
	GENERATED_BODY()

	/** Monotonic index of the fixed step that just completed (starts at 0). */
	UPROPERTY(BlueprintReadOnly, Category = "SimEconomy|Economy")
	int64 StepIndex = 0;

	/** Fixed-step duration in simulation seconds. */
	UPROPERTY(BlueprintReadOnly, Category = "SimEconomy|Economy")
	double StepSeconds = 0.0;

	/** Sim-clock day number at the time the step completed. */
	UPROPERTY(BlueprintReadOnly, Category = "SimEconomy|Economy")
	int32 DayNumber = 0;
};

/**
 * World-scoped, server-authoritative FIXED-STEP economy driver.
 *
 * Drives a fixed-step accumulator off the shared ISeam_SimClock (resolved from the service locator):
 *   ScaledDelta = RealDelta * Clock->GetTimeScale(); accumulation is skipped while Clock->IsPaused().
 * Each whole fixed step advances every registered participant (ISimEco_StepListener — facility
 * queues, consumer drains) by one step, runs the market clearing (Market->ClearMarket), and
 * broadcasts Bus_TickCompleted.
 *
 * Ticking uses an FTSTicker (NOT FTickableGameObject on the subsystem) so the driver never ticks in
 * the editor or during seamless travel; the ticker is removed in Deinitialize. The whole driver is
 * authority-only: on a client it registers no ticker and advances nothing.
 *
 * Participants register themselves with the driver (server side) so the driver owns iteration order
 * and never hard-depends on a concrete component type.
 */
UCLASS()
class DESIGNPATTERNSSIMECONOMY_API USimEco_EconomySubsystem : public UDP_WorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/** UWorldSubsystem has no HasWorldAuthority of its own — declare our own. */
	bool HasWorldAuthority() const
	{
		const UWorld* W = GetWorld();
		return W && W->GetNetMode() != NM_Client;
	}

	//~ Begin UDP_WorldSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_WorldSubsystem

	/**
	 * Register a step participant (facility queue, consumer drain) to be advanced once per fixed step.
	 * AUTHORITY ONLY. Safe to call with an already-registered participant (deduplicated). The driver
	 * holds participants as TScriptInterface and prunes any that become stale.
	 */
	void RegisterStepListener(const TScriptInterface<ISimEco_StepListener>& Listener);

	/** Stop advancing a step participant. AUTHORITY ONLY. */
	void UnregisterStepListener(const TScriptInterface<ISimEco_StepListener>& Listener);

	/**
	 * Explicitly bind the sim-clock seam (e.g. when the owner of the clock wants to inject it
	 * directly instead of via the service locator). AUTHORITY ONLY. Pass an empty interface to clear
	 * and fall back to service-locator resolution.
	 */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Economy")
	void SetSimClock(const TScriptInterface<ISeam_SimClock>& InClock);

	/** Number of completed fixed steps since this world started. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimEconomy|Economy")
	int64 GetStepIndex() const { return StepIndex; }

	/** The fixed-step duration in simulation seconds (from developer settings). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimEconomy|Economy")
	double GetFixedStepSeconds() const { return FixedStepSeconds; }

private:
	/** Participants advanced each fixed step. Server-only; held as seam interfaces. */
	UPROPERTY(Transient)
	TArray<TScriptInterface<ISimEco_StepListener>> StepListeners;

	/** Explicitly-injected clock; when unset we resolve from the service locator each tick. */
	UPROPERTY(Transient)
	TScriptInterface<ISeam_SimClock> InjectedClock;

	/** Cached market subsystem (world-scoped sibling); re-resolved if it goes stale. */
	UPROPERTY(Transient)
	TWeakObjectPtr<USimEco_MarketSubsystem> CachedMarket;

	/** FTSTicker handle driving the accumulator (server only). */
	FTSTicker::FDelegateHandle TickerHandle;

	/** Unconsumed simulation time carried between real frames, in simulation seconds. */
	double Accumulator = 0.0;

	/** Fixed-step size in simulation seconds (snapshotted from settings at Initialize). */
	double FixedStepSeconds = 1.0;

	/** Number of fixed steps completed. */
	int64 StepIndex = 0;

	/** Per-real-frame ticker callback: accumulates scaled time and runs whole fixed steps. */
	bool TickAccumulator(float RealDeltaSeconds);

	/** Run exactly one fixed step: advance producers, clear the market, broadcast Bus_TickCompleted. */
	void AdvanceOneStep();

	/** Resolve the active clock: the injected one if set, else the service-locator-published seam. */
	TScriptInterface<ISeam_SimClock> ResolveClock() const;

	/** Resolve (and cache) the world's market subsystem. */
	USimEco_MarketSubsystem* ResolveMarket();
};
