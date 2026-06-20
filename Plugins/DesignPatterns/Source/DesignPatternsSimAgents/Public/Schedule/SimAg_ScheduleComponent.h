// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Schedule/SimAg_Scheduler.h"
#include "Schedule/SimAg_ScheduleAsset.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "SimAg_ScheduleComponent.generated.h"

class USimAg_ClockSubsystem;

/** Fired locally when this component's resolved current activity changes (server and clients). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSimAg_OnActivityChanged, FGameplayTag, NewActivity, FGameplayTag, NewLocation);

/**
 * Per-agent schedule driver. Holds a schedule (by data-registry tag) and resolves the agent's
 * current activity/location from the world sim clock, re-evaluating on HOUR EDGES rather than every
 * frame (the clock fires OnHourChanged). Implements ISimAg_Scheduler so the agent brain reads the
 * current activity through the seam without knowing this concrete type.
 *
 * REPLICATION model: the schedule INPUT (which schedule asset, by tag) is the only authoritative
 * state, and it changes rarely — it replicates as a simple ReplicatedUsing property. The RESOLVED
 * activity is DERIVED on both server and clients from (replicated schedule tag) + (clock time, which
 * clients already reproduce via FSimAg_ClockSnapshot), so the per-hour result need not be replicated
 * at all. Every mutator of the authoritative schedule tag guards authority at the top.
 */
UCLASS(ClassGroup = (DesignPatternsSimAgents), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSSIMAGENTS_API USimAg_ScheduleComponent : public UActorComponent, public ISimAg_Scheduler
{
	GENERATED_BODY()

public:
	USimAg_ScheduleComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent

	//~ Begin ISimAg_Scheduler
	virtual FGameplayTag GetCurrentActivity_Implementation() const override;
	virtual FSimAg_ScheduleEntry GetScheduleEntryForHour_Implementation(float HourOfDay) const override;
	//~ End ISimAg_Scheduler

	/**
	 * Assign the active schedule by its data-registry tag. AUTHORITY ONLY: early-returns on clients.
	 * Triggers an immediate refresh on the server and replicates the tag (OnRep refreshes clients).
	 */
	UFUNCTION(BlueprintCallable, Category = "SimAgents|Schedule")
	void SetScheduleTag(FGameplayTag InScheduleTag);

	/** The currently assigned schedule tag (authoritative, replicated). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimAgents|Schedule")
	FGameplayTag GetScheduleTag() const { return ScheduleTag; }

	/** The location the current activity wants the agent at (empty if none/unscheduled). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimAgents|Schedule")
	FGameplayTag GetCurrentLocation() const { return CurrentLocation; }

	/**
	 * Re-resolve the current activity from the clock and the assigned schedule. Safe to call on
	 * server and clients (it is a pure derivation from already-available state). Authority-guarded
	 * only where it would broadcast a bus event (server owns the canonical change notification); the
	 * local OnActivityChanged delegate fires on both sides so UI/AI react everywhere.
	 */
	UFUNCTION(BlueprintCallable, Category = "SimAgents|Schedule")
	void RefreshFromClock();

	/** Fired (server and clients) when the resolved current activity changes. */
	UPROPERTY(BlueprintAssignable, Category = "SimAgents|Schedule")
	FSimAg_OnActivityChanged OnActivityChanged;

protected:
	/** OnRep for the replicated schedule tag: re-resolve on the client when the assignment changes. */
	UFUNCTION()
	void OnRep_ScheduleTag();

private:
	/**
	 * Authoritative: which schedule asset (by data-registry DataTag) this agent follows. Replicated
	 * so clients can derive the same activity locally. Changes rarely (assignment time), so a plain
	 * ReplicatedUsing property is the right tool — no fast array needed.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_ScheduleTag)
	FGameplayTag ScheduleTag;

	/** Cached, resolved schedule asset for ScheduleTag (lazy-loaded via the data registry). */
	UPROPERTY(Transient)
	TObjectPtr<USimAg_ScheduleAsset> ResolvedSchedule;

	/** Last resolved activity, tracked to fire OnActivityChanged exactly on changes. */
	UPROPERTY(Transient)
	FGameplayTag CurrentActivity;

	/** Location of the current activity. */
	UPROPERTY(Transient)
	FGameplayTag CurrentLocation;

	/** Weak, non-owning handle to the world clock; resolved lazily and null-checked before deref. */
	UPROPERTY(Transient)
	TWeakObjectPtr<USimAg_ClockSubsystem> CachedClock;

	/** Bound to the clock's OnHourChanged so we re-resolve only on hour edges (not per frame). */
	UFUNCTION()
	void HandleHourChanged(int32 NewDay, int32 NewHour);

	/** Resolve (and cache) the world clock subsystem, preferring the service-locator key. */
	USimAg_ClockSubsystem* GetClock() const;

	/** Resolve (and cache) the schedule asset for ScheduleTag via the core data registry. */
	USimAg_ScheduleAsset* GetResolvedSchedule();

	/** Subscribe to the clock's hour edges (idempotent). */
	void BindClockDelegates();

	/** Unsubscribe from the clock's hour edges (called on EndPlay). */
	void UnbindClockDelegates();

	/** Apply a newly resolved entry: update Current*, fire the local delegate, and (server) the bus. */
	void ApplyResolvedEntry(const FSimAg_ScheduleEntry& Entry);
};
