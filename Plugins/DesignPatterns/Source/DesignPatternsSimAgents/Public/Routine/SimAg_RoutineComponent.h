// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Routine/SimAg_RoutineAsset.h"
#include "SimAg_RoutineComponent.generated.h"

class USimAg_ClockSubsystem;
class USimAg_ScheduleComponent;

/** Fired (server and clients) when the resolved routine step changes, or interrupt/resume happens. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSimAg_OnRoutineStepChanged, FGameplayTag, NewActivity, FGameplayTag, NewLocation);

/**
 * Per-agent daily-life driver that adds TRAVEL LEAD-TIME and INTERRUPT/RESUME on top of a routine asset.
 *
 * COMPOSITION over a second scheduler: when this component shares an actor with a USimAg_ScheduleComponent
 * it COMPOSES it (reads its ISimAg_Scheduler) rather than implementing a competing ISimAg_Scheduler on the
 * same actor (which would make seam resolution ambiguous). It owns the richer routine asset and the
 * lead-time/interrupt logic; the schedule component (if present) stays the canonical ISimAg_Scheduler.
 *
 * LEAD-TIME: re-resolving on each clock hour edge, it looks at the UPCOMING step and, using the agent's
 * travel-speed estimate (settings) and the distance to that step's location, decides whether to leave
 * EARLY so the agent ARRIVES on the hour.
 *
 * INTERRUPT/RESUME: Interrupt(Reason) records the current step as a resume point and sets bInterrupted so
 * the brain's flee strategy takes over; Resume() restores the saved step.
 *
 * REPLICATION: only RoutineTag (ReplicatedUsing) and bInterrupted replicate — the resolved step is derived
 * locally from (replicated tag) + (clock time clients already reproduce). Every mutator guards authority.
 */
UCLASS(ClassGroup = (DesignPatternsSimAgents), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSSIMAGENTS_API USimAg_RoutineComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USimAg_RoutineComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent

	/**
	 * Assign the active routine by its data-registry tag. AUTHORITY ONLY: early-returns on clients.
	 * Triggers an immediate refresh on the server and replicates the tag.
	 */
	UFUNCTION(BlueprintCallable, Category = "SimAgents|Routine")
	void SetRoutineTag(FGameplayTag InRoutineTag);

	/** The currently assigned routine tag (authoritative, replicated). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimAgents|Routine")
	FGameplayTag GetRoutineTag() const { return RoutineTag; }

	/** The routine step currently in effect (empty Activity if unresolved). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimAgents|Routine")
	FSimAg_RoutineStep GetCurrentStep() const { return CurrentStep; }

	/**
	 * Interrupt the routine (e.g. to flee). AUTHORITY ONLY. Records the current step as a resume point and
	 * sets bInterrupted. No-op if the current step is non-interruptible.
	 * @return true if the routine was actually interrupted.
	 */
	UFUNCTION(BlueprintCallable, Category = "SimAgents|Routine")
	bool Interrupt(FGameplayTag Reason);

	/** Resume the routine after an interrupt, restoring the saved step. AUTHORITY ONLY. */
	UFUNCTION(BlueprintCallable, Category = "SimAgents|Routine")
	void Resume();

	/** True while the routine is interrupted. Client-safe (replicated). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimAgents|Routine")
	bool IsInterrupted() const { return bInterrupted; }

	/**
	 * Re-resolve the current step from the clock and routine asset, applying travel lead-time toward the
	 * upcoming step. Safe on server and clients (a pure derivation). The local delegate fires on both
	 * sides; the bus event is authority-only.
	 */
	UFUNCTION(BlueprintCallable, Category = "SimAgents|Routine")
	void RefreshFromClock();

	/** Fired when the resolved routine step changes (server and clients). */
	UPROPERTY(BlueprintAssignable, Category = "SimAgents|Routine")
	FSimAg_OnRoutineStepChanged OnRoutineStepChanged;

protected:
	/** OnRep for the replicated routine tag: re-resolve on the client. */
	UFUNCTION()
	void OnRep_Routine();

private:
	/**
	 * Authoritative: which routine asset (by DataTag) this agent follows. Replicated so clients derive the
	 * same step locally. Changes rarely, so a plain ReplicatedUsing property is right.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_Routine)
	FGameplayTag RoutineTag;

	/** True while the routine is interrupted (replicated so clients suppress routine targeting too). */
	UPROPERTY(Replicated)
	bool bInterrupted = false;

	/** Cached, resolved routine asset for RoutineTag (lazy-loaded via the data registry). */
	UPROPERTY(Transient)
	TObjectPtr<USimAg_RoutineAsset> ResolvedRoutine;

	/** The step currently in effect (derived). */
	UPROPERTY(Transient)
	FSimAg_RoutineStep CurrentStep;

	/** The step saved at interrupt time, restored on Resume. */
	UPROPERTY(Transient)
	FSimAg_RoutineStep SavedStep;

	/** Whether SavedStep is meaningful (an interrupt happened). */
	UPROPERTY(Transient)
	bool bHasSavedStep = false;

	/** Weak, non-owning handle to the world clock. */
	UPROPERTY(Transient)
	TWeakObjectPtr<USimAg_ClockSubsystem> CachedClock;

	/** Cached travel-speed estimate (world units / second) from settings. */
	float TravelSpeedEstimate = 300.f;

	/** Bound to the clock's OnHourChanged so we re-resolve only on hour edges. */
	UFUNCTION()
	void HandleHourChanged(int32 NewDay, int32 NewHour);

	/** Resolve (and cache) the world clock subsystem. */
	USimAg_ClockSubsystem* GetClock() const;

	/** Resolve (and cache) the routine asset for RoutineTag via the core data registry. */
	USimAg_RoutineAsset* GetResolvedRoutine();

	/** Subscribe to the clock's hour edges (idempotent). */
	void BindClockDelegates();

	/** Unsubscribe from the clock's hour edges (called on EndPlay). */
	void UnbindClockDelegates();

	/** Apply a newly resolved step: update CurrentStep, fire the local delegate and (server) the bus. */
	void ApplyResolvedStep(const FSimAg_RoutineStep& Step);

	/** The composed schedule component on this actor, if any (read-only composition). */
	USimAg_ScheduleComponent* GetScheduleComponent() const;

	/**
	 * Estimate how many in-sim HOURS it would take the agent to travel to its next routine anchor, so the
	 * planner can leave early. Distance is taken from the agent's current location to its work/home anchor
	 * (via the ISimAg_Agent seam if present, else the actor location => 0), divided by the settings
	 * travel-speed estimate and converted to in-sim hours using the clock's day length. Returns 0 when no
	 * meaningful anchor distance is available (the agent then leaves exactly on the hour).
	 */
	float EstimateTravelHours(const FVector& AgentLocation, int32 HoursPerDay, const USimAg_ClockSubsystem* Clock) const;
};
