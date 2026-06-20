// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "Analytics_DeveloperSettings.generated.h"

class UAnalytics_EventMapDataAsset;

/**
 * Project configuration for the DesignPatternsAnalytics module. Appears under
 * Project Settings -> Plugins -> Design Patterns Analytics. All knobs are data; the module
 * hard-codes no gameplay/telemetry magic numbers.
 *
 * Everything here is per-machine/local. Nothing in this module replicates or saves into
 * gameplay save state (telemetry buffers are transient; consent is a local config bool).
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Design Patterns Analytics"))
class DESIGNPATTERNSANALYTICS_API UAnalytics_DeveloperSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UAnalytics_DeveloperSettings();

	virtual FName GetCategoryName() const override { return FName("Plugins"); }

	/** Convenience accessor for the CDO. Never null after engine init; callers still null-check defensively. */
	static const UAnalytics_DeveloperSettings* Get();

	// ---- Consent ----

	/**
	 * When true (the privacy-safe default), the subsystem records NOTHING until the game calls
	 * SetConsent(true). Buffered/observed events are discarded while consent is off. A project
	 * that has its own up-front consent flow may set this false to start in the granted state,
	 * but default-off is the recommended, regulation-friendly posture.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Consent")
	bool bConsentDefaultOff = true;

	// ---- Service resolution ----

	/**
	 * Service-locator tag under which the host registers its ISeam_AnalyticsSink adapter
	 * (the object that forwards to the engine IAnalyticsProvider). When unresolved the module
	 * falls back to the offline file sink. Anchor under DP.Service in the project tag table.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Services")
	FGameplayTag AnalyticsSinkServiceTag;

	/**
	 * Service-locator tag under which the host registers its IAnalytics_PlayerIdProvider.
	 * When unresolved, experiment bucketing falls back to a per-session random bucket.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Services")
	FGameplayTag PlayerIdProviderServiceTag;

	// ---- Bus bridge ----

	/**
	 * Bus channel (subtree) the subsystem auto-subscribes to. DP.Bus.* messages on this
	 * subtree are converted into analytics events via the event-map asset. Defaults to the
	 * core bus root so an out-of-the-box install measures everything; set to a narrower
	 * subtree to scope what analytics observes. Seeded from AnalyticsNativeTags::DefaultObservedBusChannel.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Bus Bridge")
	FGameplayTag BusChannelToObserve;

	/**
	 * Optional event-map data asset that defines bus-channel -> analytics-event conversions and
	 * attribute extraction. When unset, only the catch-all (Analytics.Event.BusUnmapped) path
	 * is used for observed bus messages. Soft so the module imposes no load cost when unused.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Bus Bridge", meta = (AllowedClasses = "/Script/DesignPatternsAnalytics.Analytics_EventMapDataAsset"))
	TSoftObjectPtr<UAnalytics_EventMapDataAsset> DefaultEventMap;

	/**
	 * If true, an observed bus message with no explicit event-map entry is still recorded as
	 * Analytics.Event.BusUnmapped (carrying the source channel as an attribute). If false,
	 * unmapped bus messages are ignored. Keeps unmapped noise out of telemetry when desired.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Bus Bridge")
	bool bRecordUnmappedBusEvents = false;

	// ---- Batching / flushing ----

	/**
	 * Number of buffered events that triggers an immediate flush. The buffer is also flushed
	 * on the periodic interval and on shutdown. Defensive floor of 1 is applied at read time.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Batching", meta = (ClampMin = "1", UIMin = "1"))
	int32 BatchSizeThreshold = 64;

	/**
	 * Seconds between periodic flushes of the event buffer. A value <= 0 disables the timer
	 * (events then flush only on the size threshold and at shutdown).
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Batching", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float FlushIntervalSeconds = 30.f;

	/**
	 * Hard cap on buffered events. If the buffer reaches this size and the sink cannot accept
	 * a flush (e.g. not ready), the OLDEST events are dropped to bound memory. Prevents an
	 * unbounded buffer when no sink is connected. Defensive floor applied at read time.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Batching", meta = (ClampMin = "1", UIMin = "1"))
	int32 MaxBufferedEvents = 4096;

	// ---- Offline file sink (safe default) ----

	/**
	 * Whether the offline file sink is used as the fallback when no ISeam_AnalyticsSink is
	 * resolved. When false and no seam sink exists, batches are dropped (still consent-gated).
	 */
	UPROPERTY(EditAnywhere, Config, Category = "File Sink")
	bool bEnableFileSinkFallback = true;

	/**
	 * Directory (under the project's Saved dir) where the offline file sink writes batched
	 * JSON-line files. Resolved against FPaths::ProjectSavedDir(). Writes happen off the game
	 * thread on plain copies only.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "File Sink")
	FString FileSinkSubdirectory = TEXT("Analytics");

	// ---- Validated accessors (apply defensive floors regardless of config edits) ----

	/** Batch threshold, floored to >= 1. */
	int32 GetEffectiveBatchSizeThreshold() const { return FMath::Max(1, BatchSizeThreshold); }

	/** Max buffered events, floored to >= batch threshold so a flush always relieves pressure. */
	int32 GetEffectiveMaxBufferedEvents() const
	{
		return FMath::Max(GetEffectiveBatchSizeThreshold(), FMath::Max(1, MaxBufferedEvents));
	}

	/** Flush interval; non-negative. <= 0 means "no periodic timer". */
	float GetEffectiveFlushInterval() const { return FMath::Max(0.f, FlushIntervalSeconds); }
};
