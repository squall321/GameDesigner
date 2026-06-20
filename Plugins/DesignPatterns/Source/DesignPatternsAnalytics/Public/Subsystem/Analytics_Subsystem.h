// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "UObject/WeakInterfacePtr.h"
#include "Containers/Ticker.h"
#include "MessageBus/DPMessage.h"
#include "Analytics/Seam_AnalyticsSink.h"
#include "Analytics_Subsystem.generated.h"

class UAnalytics_EventMapDataAsset;
class UDP_MessageBusSubsystem;
class ISeam_AnalyticsSink;

/**
 * A single buffered, fully-resolved analytics event ready to hand to a sink.
 *
 * This is a PLAIN copyable value (tag + PII-safe attributes only). It contains no UObject
 * references, so a snapshot of the buffer can be copied to a worker thread for the offline
 * file sink without touching the UObject graph off the game thread.
 */
USTRUCT()
struct FAnalytics_BufferedEvent
{
	GENERATED_BODY()

	/** The analytics event id (Analytics.Event.*). */
	UPROPERTY()
	FGameplayTag EventTag;

	/** PII-safe attributes (FSeam_NetValue values only). */
	UPROPERTY()
	TArray<FSeam_AnalyticsAttr> Attributes;

	/** FApp seconds when the event was recorded — emitted as a "t" attribute by the file sink. */
	UPROPERTY()
	double TimestampSeconds = 0.0;
};

/**
 * GameInstance-scoped analytics/telemetry subsystem.
 *
 * Responsibilities:
 *  - Consent gate (default OFF): records NOTHING until SetConsent(true). This is the privacy
 *    contract; see SetConsent / RecordEvent.
 *  - Game-thread batching: RecordEvent appends to an in-memory buffer on the game thread. The
 *    buffer flushes on a size threshold, on a periodic timer, and at shutdown.
 *  - Sink dispatch: on flush, events go to a resolved ISeam_AnalyticsSink if one is ready
 *    (held WEAKLY and pruned — a GI subsystem must never keep a hard interface ref across
 *    worlds), otherwise to the offline file sink (plain copies handed off-thread).
 *  - Bus bridge: auto-subscribes to the configured bus channel and converts DP.Bus.* messages
 *    into analytics events via the event-map asset.
 *  - Experiments: hashes the resolved stable player id (never recorded verbatim) into a stable
 *    bucket for A/B assignment.
 *
 * PII safety is structural: every attribute value is an FSeam_NetValue, which cannot hold
 * FText, a UObject, or a free-form id. The subsystem additionally never copies the raw player
 * id into an attribute — only its hash bucket.
 *
 * Threading: all public API and all buffer mutation are GAME-THREAD ONLY. The only off-thread
 * work is the file sink's disk write, which operates on a plain copied snapshot.
 */
UCLASS()
class DESIGNPATTERNSANALYTICS_API UAnalytics_Subsystem : public UDP_GameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	// ---- Consent ----

	/**
	 * Grant or revoke analytics consent. While consent is OFF the buffer is cleared and
	 * RecordEvent is a no-op, so no telemetry is produced or persisted. Granting consent for
	 * the first time records a SessionStart event. Revoking consent flushes nothing and
	 * discards any buffered events (they were recorded under granted consent, but revoking is
	 * treated as "forget pending data" for safety).
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Analytics")
	void SetConsent(bool bGranted);

	/** True if analytics recording is currently permitted. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Analytics")
	bool HasConsent() const { return bConsentGranted; }

	// ---- Recording ----

	/**
	 * Record an analytics event with PII-safe attributes. No-op unless consent is granted.
	 * Appends to the game-thread buffer; flushes if the size threshold is reached. Attributes
	 * are copied by value (no UObject capture).
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Analytics")
	void RecordEvent(FGameplayTag EventTag, const TArray<FSeam_AnalyticsAttr>& Attributes);

	/** Convenience: record an event with no attributes. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Analytics")
	void RecordSimpleEvent(FGameplayTag EventTag);

	/** Force an immediate flush of the buffered events to the active sink. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Analytics")
	void Flush();

	// ---- Experiments ----

	/**
	 * Deterministically assign the current player to one of NumBuckets for an experiment.
	 * Uses a stable hash of (resolved player id + ExperimentTag); returns a stable bucket index
	 * in [0, NumBuckets). If no player-id provider is resolved, falls back to a per-session
	 * random bucket (still stable for the rest of the session). Never records the raw id.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Analytics")
	int32 GetExperimentBucket(FGameplayTag ExperimentTag, int32 NumBuckets) const;

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

private:
	// ---- Consent state ----

	/** Current consent. Seeded from settings (default OFF) on Initialize. */
	bool bConsentGranted = false;

	/** True once a SessionStart event has been recorded for this granted-consent session. */
	bool bSessionStartRecorded = false;

	// ---- Event buffer (game thread only) ----

	/** Pending events awaiting flush. Bounded by settings; oldest dropped on overflow. */
	UPROPERTY(Transient)
	TArray<FAnalytics_BufferedEvent> EventBuffer;

	// ---- Sink (held WEAK; pruned on use) ----

	/**
	 * Weakly-held analytics sink resolved from the service locator. A GI subsystem must NOT
	 * keep a TScriptInterface hard ref to a possibly world-scoped provider across worlds, so we
	 * store the UObject weakly plus a raw interface pointer and re-validate (prune) on every use.
	 */
	TWeakObjectPtr<UObject> SinkObjectWeak;
	ISeam_AnalyticsSink* SinkInterface = nullptr;

	// ---- Bus bridge ----

	/** Cached message bus pointer (GI-scoped sibling subsystem; safe for the GI's lifetime). */
	UPROPERTY(Transient)
	TObjectPtr<UDP_MessageBusSubsystem> MessageBus = nullptr;

	/** Handle for our bus subscription, removed on Deinitialize. */
	FDP_ListenerHandle BusListenerHandle;

	/** Resolved event-map asset (synchronously loaded from settings on first need). Strong ref. */
	UPROPERTY(Transient)
	TObjectPtr<UAnalytics_EventMapDataAsset> EventMap = nullptr;

	/** True once we have attempted to load the event map (so we don't retry a missing asset). */
	bool bEventMapResolved = false;

	// ---- Flush timer ----

	/** FTSTicker handle driving periodic flushes. Removed on Deinitialize. */
	FTSTicker::FDelegateHandle FlushTickerHandle;

	// ---- Experiment session salt ----

	/** Random salt minted once per session; used for bucketing when no stable id is available. */
	uint32 SessionSalt = 0;

	// ---- Internal helpers ----

	/** Re-resolve the sink seam from the service locator and prune a dead weak ref. */
	void RefreshSink();

	/** True if a live, ready sink is currently connected. Prunes a dead weak ref as a side effect. */
	bool HasLiveSink();

	/** Subscribe to the configured bus channel (if a message bus and a valid channel exist). */
	void SubscribeToBus();

	/** Handle a bus message: convert via the event map and record the resulting analytics event. */
	void HandleBusMessage(const FDP_Message& Message);

	/** Resolve the event-map asset from settings once (synchronous load). */
	void EnsureEventMapResolved();

	/** Push the whole buffer to the live seam sink, or to the offline file sink. Clears on success. */
	void FlushInternal();

	/** Write a snapshot of buffered events to the offline file sink off the game thread. */
	void FlushToFileSink(const TArray<FAnalytics_BufferedEvent>& Snapshot);

	/** Resolve the stable player id from the seam, or empty when unresolved. */
	FString ResolvePlayerId() const;

	/** Drop oldest events until the buffer is within the configured cap. */
	void EnforceBufferCap();

	/** FTSTicker callback: periodic flush. */
	bool TickFlush(float DeltaTime);

	/** Serialize one buffered event to a single JSON line (for the file sink). */
	static FString EventToJsonLine(const FAnalytics_BufferedEvent& Event);
};
