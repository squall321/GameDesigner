// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "Clock/Seam_SimClock.h"
#include "Clock/SimAg_TimeSource.h"
#include "UObject/ScriptInterface.h"
#include "Containers/Ticker.h"
#include "SimAg_ClockSubsystem.generated.h"

/**
 * A calendar instant inside the simulation: which Day, and the Hour/Minute within that day.
 *
 * Hour is in [0, HoursPerDay) and Minute is in [0, 60). The clock derives this from a normalized
 * time-of-day in [0,1): Hour = floor(t * HoursPerDay), Minute = floor(frac * 60). This is the
 * designer-facing time used by schedules; it is NOT replicated as a struct — clients receive the
 * compact FSimAg_ClockSnapshot and recompute the calendar locally (zero per-frame replication).
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMAGENTS_API FSimAg_DateTime
{
	GENERATED_BODY()

	/** Whole days elapsed since simulation start (calendar day index, 0-based). */
	UPROPERTY(BlueprintReadOnly, Category = "SimAgents|Clock")
	int32 Day = 0;

	/** Hour within the day, in [0, HoursPerDay). */
	UPROPERTY(BlueprintReadOnly, Category = "SimAgents|Clock")
	int32 Hour = 0;

	/** Minute within the hour, in [0, 60). */
	UPROPERTY(BlueprintReadOnly, Category = "SimAgents|Clock")
	int32 Minute = 0;

	FSimAg_DateTime() = default;
	FSimAg_DateTime(int32 InDay, int32 InHour, int32 InMinute)
		: Day(InDay), Hour(InHour), Minute(InMinute) {}

	bool operator==(const FSimAg_DateTime& Other) const
	{
		return Day == Other.Day && Hour == Other.Hour && Minute == Other.Minute;
	}
	bool operator!=(const FSimAg_DateTime& Other) const { return !(*this == Other); }

	/** Human-readable "DDD HH:MM" form for logging/debug. */
	FString ToString() const
	{
		return FString::Printf(TEXT("D%d %02d:%02d"), Day, Hour, Minute);
	}
};

/**
 * Compact authoritative clock state the SERVER hands to clients (via SyncFromServer). Carrying this
 * tiny struct on a replicated carrier — never the subsystem — lets clients reproduce the exact same
 * time locally and advance it smoothly between syncs, so the per-frame replication cost is ZERO.
 *
 * Fractional day is the single source of truth: DayNumber + NormalizedTimeOfDay together. Time scale
 * and pause are included so a client's local extrapolation honours them between snapshots.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMAGENTS_API FSimAg_ClockSnapshot
{
	GENERATED_BODY()

	/** Whole days elapsed since simulation start at the moment of the snapshot. */
	UPROPERTY(BlueprintReadWrite, Category = "SimAgents|Clock")
	int32 DayNumber = 0;

	/** Time of day in [0,1) at the moment of the snapshot. */
	UPROPERTY(BlueprintReadWrite, Category = "SimAgents|Clock")
	float NormalizedTimeOfDay = 0.f;

	/** Authoritative time scale in effect (so the client extrapolates at the right speed). */
	UPROPERTY(BlueprintReadWrite, Category = "SimAgents|Clock")
	float TimeScale = 1.f;

	/** Whether the authoritative clock is paused (client freezes its extrapolation if so). */
	UPROPERTY(BlueprintReadWrite, Category = "SimAgents|Clock")
	bool bPaused = false;
};

/** Fired (server and clients) when the calendar Hour edge changes — the cheap signal schedules use. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSimAg_OnHourChanged, int32, NewDay, int32, NewHour);

/** Fired (server and clients) when a new day begins (Day index increments). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSimAg_OnDayChanged, int32, NewDay);

/**
 * World simulation clock. The single owner of simulation time for a world, and the implementer of
 * the shared ISeam_SimClock seam — every consumer (schedules, economy steps, grid growth) reads
 * time through TScriptInterface<ISeam_SimClock> and never depends on this concrete type.
 *
 * TWO OPERATING MODES, chosen by whether an external time source is bound:
 *   - UNBOUND (bOwnsTime = true): this subsystem OWNS authoritative time. On the server it runs an
 *     FTSTicker that advances a fractional-day accumulator by RealDelta * TimeScale (skipping while
 *     paused). The ticker is registered in Initialize and REMOVED in Deinitialize.
 *   - BOUND (bOwnsTime = false): an ISimAg_TimeSource (e.g. a Survival day/night clock adapter) owns
 *     the phase; this subsystem DERIVES its calendar from the source every read and runs NO ticker.
 *
 * REPLICATION: subsystems are NEVER replicated. The server advances time and a replicated carrier
 * (component/AInfo) periodically calls SyncFromServer(FSimAg_ClockSnapshot) on the client copy of
 * this subsystem; clients then locally extrapolate between syncs. There is no per-frame clock rep.
 *
 * The subsystem registers itself with the service locator under SimAgNativeTags::Service_Clock so
 * any system can resolve the clock seam by stable tag.
 */
UCLASS()
class DESIGNPATTERNSSIMAGENTS_API USimAg_ClockSubsystem : public UDP_WorldSubsystem, public ISeam_SimClock
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	//~ Begin ISeam_SimClock (the shared clock seam this subsystem fulfils)
	virtual double GetTimeScale_Implementation() const override;
	virtual bool IsPaused_Implementation() const override;
	virtual float GetNormalizedTimeOfDay_Implementation() const override;
	virtual int32 GetDayNumber_Implementation() const override;
	//~ End ISeam_SimClock

	//~ Begin UDP_WorldSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_WorldSubsystem

	/**
	 * Authority helper. UWorldSubsystem has NO HasWorldAuthority of its own, so we declare our own:
	 * a world has authority when its net mode is not a pure client. All time-mutating APIs guard on it.
	 */
	bool HasWorldAuthority() const
	{
		const UWorld* W = GetWorld();
		return W && W->GetNetMode() != NM_Client;
	}

	/**
	 * Bind an external time source (e.g. a Survival clock adapter). Switches the clock into DERIVED
	 * mode: bOwnsTime becomes false, the authority ticker is removed, and every time read is computed
	 * from the source. AUTHORITY ONLY (the binding mirrors to clients implicitly via the source's own
	 * replication; clients in pure-derived setups simply read the source too). Passing an invalid /
	 * null interface UNBINDS and returns the clock to owned mode (re-registering the ticker on authority).
	 */
	UFUNCTION(BlueprintCallable, Category = "SimAgents|Clock")
	void SetExternalTimeSource(const TScriptInterface<ISimAg_TimeSource>& InSource);

	/** True when an external time source is currently bound (the clock is in derived mode). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimAgents|Clock")
	bool HasExternalTimeSource() const;

	/**
	 * Set the simulation time multiplier (1 = real time, 0 = effectively paused). AUTHORITY ONLY:
	 * early-returns on clients. No effect while an external source is bound (the source owns speed).
	 */
	UFUNCTION(BlueprintCallable, Category = "SimAgents|Clock")
	void SetTimeScale(float InTimeScale);

	/** Pause/unpause owned simulation time. AUTHORITY ONLY: early-returns on clients. */
	UFUNCTION(BlueprintCallable, Category = "SimAgents|Clock")
	void SetPaused(bool bInPaused);

	/** Current full calendar instant (Day/Hour/Minute), derived from the live normalized time. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimAgents|Clock")
	FSimAg_DateTime GetDateTime() const;

	/** Number of hours in one in-sim day (from settings; cached at Initialize, min 1). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimAgents|Clock")
	int32 GetHoursPerDay() const { return HoursPerDay; }

	/** Number of in-sim days that make up one season (designer tunable; for season-aware systems). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimAgents|Clock")
	int32 GetDaysPerSeason() const { return DaysPerSeason; }

	/** The current season index = floor(DayNumber / DaysPerSeason). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimAgents|Clock")
	int32 GetSeasonNumber() const;

	/** Build a snapshot of the current authoritative state, for handing to clients. AUTHORITY makes sense. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimAgents|Clock")
	FSimAg_ClockSnapshot MakeSnapshot() const;

	/**
	 * CLIENT entry point: adopt an authoritative snapshot pushed from the server's replicated carrier.
	 * Sets the local accumulator/scale/pause so the client's reads match the server, then extrapolates
	 * locally between syncs. No-op on authority (the server is its own source of truth). Fires the
	 * hour/day edge delegates if the adopted time crossed an edge.
	 */
	UFUNCTION(BlueprintCallable, Category = "SimAgents|Clock")
	void SyncFromServer(const FSimAg_ClockSnapshot& Snapshot);

	/** Fired when the calendar Hour changes (server and clients). Schedules listen to this. */
	UPROPERTY(BlueprintAssignable, Category = "SimAgents|Clock")
	FSimAg_OnHourChanged OnHourChanged;

	/** Fired when a new day starts (server and clients). */
	UPROPERTY(BlueprintAssignable, Category = "SimAgents|Clock")
	FSimAg_OnDayChanged OnDayChanged;

private:
	/**
	 * Authoritative fractional time, in DAYS, since simulation start. Integer part is the day index,
	 * fractional part * HoursPerDay gives the hour. This is the owned-mode source of truth; in derived
	 * mode it is recomputed from the external source each read so debug/extrapolation stay consistent.
	 */
	double FractionalDays = 0.0;

	/** Owned-mode time multiplier. Ignored while an external source is bound. */
	float TimeScale = 1.f;

	/** Owned-mode pause flag. Ignored while an external source is bound. */
	bool bPaused = false;

	/** True when this subsystem owns time (no external source bound) and may run the ticker. */
	bool bOwnsTime = true;

	/** Hours in one in-sim day (cached from settings, clamped >= 1). */
	int32 HoursPerDay = 24;

	/** In-sim days per season (cached from settings, clamped >= 1). */
	int32 DaysPerSeason = 28;

	/**
	 * The bound external time source, if any. Stored as a script interface so it can be a C++ or BP
	 * implementer; non-owning (the source lives elsewhere — e.g. a Survival subsystem), so it is held
	 * as a script interface and always null-checked before use.
	 */
	UPROPERTY(Transient)
	TScriptInterface<ISimAg_TimeSource> ExternalTimeSource;

	/** Handle to the owned-mode authority ticker (only valid in owned mode on authority). */
	FTSTicker::FDelegateHandle TickerHandle;

	/** Last Day/Hour observed, used to detect edges and fire OnHourChanged/OnDayChanged exactly once. */
	int32 LastObservedDay = -1;
	int32 LastObservedHour = -1;

	/** Register the owned-mode authority ticker if appropriate (owned mode + authority + not running). */
	void EnsureTickerRunning();

	/** Remove the owned-mode authority ticker if it is running. */
	void StopTicker();

	/** The FTSTicker callback: advances FractionalDays by RealDelta * TimeScale (unless paused). */
	bool TickClock(float RealDeltaSeconds);

	/** Read the live normalized time-of-day in [0,1) from the active source (external or owned). */
	float ComputeNormalizedTimeOfDay() const;

	/** Read the live whole-day index from the active source (external or owned). */
	int32 ComputeDayNumber() const;

	/** Recompute current Day/Hour, fire edge delegates if they changed, and update Last* trackers. */
	void DetectAndBroadcastEdges();

	/** Register this clock with the service locator under SimAgNativeTags::Service_Clock. */
	void RegisterClockService();
};
