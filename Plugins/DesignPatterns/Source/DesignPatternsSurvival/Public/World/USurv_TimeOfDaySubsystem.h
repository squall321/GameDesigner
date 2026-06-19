// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "Containers/Ticker.h"
#include "USurv_TimeOfDaySubsystem.generated.h"

/** Coarse phase of the day/night cycle, derived from the normalized time-of-day. */
UENUM(BlueprintType)
enum class ESurv_DayPhase : uint8
{
	Dawn  UMETA(DisplayName = "Dawn"),
	Day   UMETA(DisplayName = "Day"),
	Dusk  UMETA(DisplayName = "Dusk"),
	Night UMETA(DisplayName = "Night")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSurv_OnPhaseChanged, ESurv_DayPhase, NewPhase);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSurv_OnNewDay, int32, DayNumber);

/**
 * Day/night cycle clock as a world subsystem.
 *
 * Derives from the core UDP_WorldSubsystem (world-scoped lifetime, torn down with the level).
 * The authority advances time on a frame ticker (FTSTicker — the subsystem is not itself
 * tickable, matching the core message-bus pattern) and fires OnPhaseChanged on phase edges.
 *
 * REPLICATION APPROACH (documented per HARD RULE): a UWorldSubsystem is not a replicated UObject,
 * so we DO NOT push the float across the wire. Instead the authority's normalized time-of-day is
 * the single source of truth; clients derive a visually-consistent value locally by advancing the
 * same DayLengthSeconds clock from a server-stamped baseline applied via SyncFromServer (call it
 * from a replicated actor/GameState's OnRep). This keeps bandwidth at zero while staying in sync
 * to within network latency — exactly what a cosmetic sky clock needs.
 */
UCLASS()
class DESIGNPATTERNSSURVIVAL_API USurv_TimeOfDaySubsystem : public UDP_WorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/** Current time of day, normalized 0..1 (0 = midnight, 0.5 = noon). Valid on server and clients. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Survival|TimeOfDay")
	float GetNormalizedTimeOfDay() const { return NormalizedTimeOfDay; }

	/** Current coarse phase. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Survival|TimeOfDay")
	ESurv_DayPhase GetCurrentPhase() const { return CurrentPhase; }

	/** Whole days elapsed since the cycle started. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Survival|TimeOfDay")
	int32 GetDayNumber() const { return DayNumber; }

	/** Real seconds for one full in-game day. Larger = slower cycle. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Survival|TimeOfDay")
	float GetDayLengthSeconds() const { return DayLengthSeconds; }

	/** Set the real-seconds length of a full day. AUTHORITY-ONLY (no-op without world authority). */
	UFUNCTION(BlueprintCallable, Category = "Survival|TimeOfDay")
	void SetDayLengthSeconds(float Seconds);

	/** Set the normalized time directly (e.g. "sleep until morning"). AUTHORITY-ONLY. */
	UFUNCTION(BlueprintCallable, Category = "Survival|TimeOfDay")
	void SetNormalizedTimeOfDay(float NewValue);

	/**
	 * Apply a server-authoritative snapshot on a client. Call from a replicated carrier's OnRep
	 * (e.g. GameState) so clients converge on the server clock without per-frame replication.
	 */
	UFUNCTION(BlueprintCallable, Category = "Survival|TimeOfDay")
	void SyncFromServer(float ServerNormalizedTime, int32 ServerDayNumber, float ServerDayLengthSeconds);

	/** Fired when the coarse phase changes (Dawn/Day/Dusk/Night). */
	UPROPERTY(BlueprintAssignable, Category = "Survival|TimeOfDay")
	FSurv_OnPhaseChanged OnPhaseChanged;

	/** Fired when the clock wraps past midnight into a new day. */
	UPROPERTY(BlueprintAssignable, Category = "Survival|TimeOfDay")
	FSurv_OnNewDay OnNewDay;

	//~ Begin UDP_WorldSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_WorldSubsystem

protected:
	/** Normalized time of day, 0..1. */
	UPROPERTY()
	float NormalizedTimeOfDay = 0.25f; // start at dawn

	/** Real seconds for a full day cycle. */
	UPROPERTY(EditDefaultsOnly, Category = "Survival|TimeOfDay", meta = (ClampMin = "1.0"))
	float DayLengthSeconds = 1200.f; // 20 real minutes per day by default

	/** Elapsed whole days. */
	UPROPERTY()
	int32 DayNumber = 0;

	/** Current coarse phase, recomputed from NormalizedTimeOfDay. */
	UPROPERTY()
	ESurv_DayPhase CurrentPhase = ESurv_DayPhase::Dawn;

	/** FTSTicker handle advancing the clock on the authority. */
	FTSTicker::FDelegateHandle TickerHandle;

	/** True if this subsystem's world is the network authority (server / standalone). */
	bool HasWorldAuthority() const;

	/** Map a normalized time to a coarse phase. */
	static ESurv_DayPhase PhaseFromNormalizedTime(float Normalized);

	/** Advance the clock by DeltaTime (authority only); fire phase/day edges. Returns true to keep ticking. */
	bool Tick(float DeltaTime);

	/** Recompute CurrentPhase from NormalizedTimeOfDay and fire OnPhaseChanged on a change. */
	void UpdatePhase();
};
