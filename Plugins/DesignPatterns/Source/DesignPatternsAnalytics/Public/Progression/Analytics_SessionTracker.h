// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "MessageBus/DPMessage.h"
#include "Analytics_SessionTracker.generated.h"

class UAnalytics_Subsystem;
class UDP_MessageBusSubsystem;

/**
 * GameInstance-scoped session lifecycle tracker.
 *
 * A "session" is the lifetime of this GameInstance subsystem (it survives level travel, which is
 * exactly the right boundary for a play session). The tracker:
 *  - records the session start time and accumulates total session playtime;
 *  - listens on the suspend/resume BUS channels (configured by tag) so it can emit a
 *    session-summary event when the OS backgrounds/suspends the app — WITHOUT taking a compile
 *    dependency on the Platform module. The host (or the Platform module) re-broadcasts the OS
 *    suspend/resume signal onto Analytics.Bus.App.Suspend / .Resume and we subscribe by tag;
 *  - also emits the summary on a clean GameInstance shutdown (Deinitialize) as a backstop, so a
 *    desktop exit that never fires a suspend signal still produces a summary.
 *
 * All state is local/per-machine and transient. Nothing here replicates or saves into gameplay
 * save state. Recording flows through the consent-gated UAnalytics_Subsystem, so with consent OFF
 * the tracker still measures locally but emits nothing.
 */
UCLASS()
class DESIGNPATTERNSANALYTICS_API UAnalytics_SessionTracker : public UDP_GameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/** Wall-clock seconds since this session started (FApp time, monotonic, pause-aware below). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Analytics|Session")
	double GetSessionDurationSeconds() const;

	/**
	 * Accumulated FOREGROUND playtime in seconds, i.e. session duration minus time spent suspended.
	 * This is the headline "how long did they play" metric the summary reports.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Analytics|Session")
	double GetForegroundPlaytimeSeconds() const;

	/** True while the app is considered suspended/backgrounded (between Suspend and Resume signals). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Analytics|Session")
	bool IsSuspended() const { return bSuspended; }

	/** Number of summary events emitted this session (suspend summaries + the final one). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Analytics|Session")
	int32 GetSummaryEmitCount() const { return SummaryEmitCount; }

	/**
	 * Emit a session-summary analytics event now (duration, foreground playtime, suspend count).
	 * Called automatically on suspend and on shutdown; exposed for an explicit checkpoint too.
	 * @param SummaryReason A tag describing why the summary was emitted (suspend / shutdown / manual).
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Analytics|Session")
	void EmitSessionSummary(FGameplayTag SummaryReason);

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

protected:
	/** Resolve the consent-gated core analytics subsystem from this GameInstance. */
	UAnalytics_Subsystem* ResolveAnalyticsSubsystem();

	/** Subscribe to the suspend/resume bus channels (by configured tag). */
	void SubscribeToAppLifecycleBus();

	/** Bus handler for the suspend signal: accumulate foreground time and emit a summary. */
	void HandleAppSuspend(const FDP_Message& Message);

	/** Bus handler for the resume signal: start a new foreground segment. */
	void HandleAppResume(const FDP_Message& Message);

private:
	/** FApp time when the session started. */
	double SessionStartTime = 0.0;

	/** FApp time when the current foreground segment started (reset on resume). */
	double ForegroundSegmentStart = 0.0;

	/** Foreground seconds accumulated from completed (suspended) segments. */
	double AccumulatedForegroundSeconds = 0.0;

	/** True between a suspend and the next resume. */
	bool bSuspended = false;

	/** How many times the app has been suspended this session. */
	int32 SuspendCount = 0;

	/** How many summary events we have emitted (for debug + dedup of the shutdown backstop). */
	int32 SummaryEmitCount = 0;

	/** True once we have emitted a summary for the CURRENT suspended state (avoid duplicate on shutdown). */
	bool bSummaryEmittedForCurrentState = false;

	/** Cached message bus (GI-scoped sibling subsystem; safe for the GI lifetime). */
	UPROPERTY(Transient)
	TObjectPtr<UDP_MessageBusSubsystem> MessageBus = nullptr;

	/** Weakly-cached core analytics subsystem. */
	UPROPERTY(Transient)
	TWeakObjectPtr<UAnalytics_Subsystem> CachedAnalyticsSubsystem;

	/** Bus listener handles, removed on Deinitialize. */
	FDP_ListenerHandle SuspendListenerHandle;
	FDP_ListenerHandle ResumeListenerHandle;

	/** Fold the current open foreground segment into the accumulator (idempotent while suspended). */
	void CloseForegroundSegment(double Now);
};
