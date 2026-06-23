// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "MessageBus/DPMessage.h"
#include "Analytics/Seam_AnalyticsSink.h"
#include "Analytics_BreadcrumbSubsystem.generated.h"

class UAnalytics_Subsystem;
class UAnalytics_TelemetryDataAsset;
class UDP_MessageBusSubsystem;

/**
 * One breadcrumb in the crash/error trail.
 *
 * PLAIN copyable value (tag + timestamp + PII-safe attributes only); no UObject refs, so a snapshot
 * of the ring can be copied to a worker thread for the crash-context file write — mirroring
 * FAnalytics_BufferedEvent.
 */
USTRUCT()
struct FAnalytics_Breadcrumb
{
	GENERATED_BODY()

	/** What happened (e.g. a bus channel or a manual crumb tag). */
	UPROPERTY()
	FGameplayTag Tag;

	/** FApp seconds when the crumb was left. */
	UPROPERTY()
	double TimestampSeconds = 0.0;

	/** PII-safe context (FSeam_NetValue values only). */
	UPROPERTY()
	TArray<FSeam_AnalyticsAttr> Attrs;
};

/**
 * GameInstance-scoped crash/error breadcrumb subsystem.
 *
 * Maintains a fixed-size RING buffer (size from the telemetry data asset) of FAnalytics_Breadcrumb.
 * The ring ALWAYS fills (it records context before consent so a crash dump is useful even from an
 * unconsented session — this is local-only context, never transmitted), but AttachToReport only
 * EMITS / persists when consent is granted: it takes a game-thread snapshot of the ring, writes a
 * crash-context file off the game thread (Async ThreadPool, capture by value — never 'this'), and
 * records an Analytics.Event.Crash.BreadcrumbAttached marker through the consent-gated core subsystem.
 *
 * It owns its OWN bus listener (a distinct FDP_ListenerHandle — NOT the core subsystem's private
 * listener) on the configured channel so bus activity auto-leaves crumbs. The listener is removed in
 * Deinitialize (HARD RULE 3: every bus-listener registered is removed on teardown).
 *
 * All state is local/per-machine and transient; nothing replicates or saves into gameplay state.
 */
UCLASS()
class DESIGNPATTERNSANALYTICS_API UAnalytics_BreadcrumbSubsystem : public UDP_GameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/** Leave a breadcrumb with PII-safe context. Always recorded into the local ring (pre-consent). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Analytics|Crash")
	void Leave(FGameplayTag CrumbTag, const TArray<FSeam_AnalyticsAttr>& Attrs);

	/** Convenience: leave a breadcrumb with no context. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Analytics|Crash")
	void LeaveSimple(FGameplayTag CrumbTag);

	/** A copy of the current trail, oldest-first. Off-thread-copyable plain values. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Analytics|Crash")
	TArray<FAnalytics_Breadcrumb> GetTrail() const;

	/**
	 * Attach the current trail to a crash/error report: snapshot on the game thread, write a
	 * crash-context file off-thread, and (when consent is granted) record a
	 * Crash.BreadcrumbAttached marker. The file is written regardless of consent (local context),
	 * but the analytics marker is consent-gated by the core subsystem.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Analytics|Crash")
	void AttachToReport(FGameplayTag ReportReason);

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

private:
	/** The ring of breadcrumbs (capped at the data-asset ring size; oldest dropped on overflow). */
	UPROPERTY(Transient)
	TArray<FAnalytics_Breadcrumb> Ring;

	/** Cached message bus (GI-scoped sibling subsystem; valid for the GI lifetime). */
	UPROPERTY(Transient)
	TObjectPtr<UDP_MessageBusSubsystem> MessageBus = nullptr;

	/** OUR own bus subscription handle (distinct from the core subsystem's), removed on Deinitialize. */
	FDP_ListenerHandle BusListenerHandle;

	/** Weakly-cached core analytics subsystem (re-resolved if it goes away). */
	UPROPERTY(Transient)
	TWeakObjectPtr<UAnalytics_Subsystem> CachedAnalyticsSubsystem;

	/** Lazily-resolved telemetry data asset (weak; owned by the asset manager). */
	UPROPERTY(Transient)
	TWeakObjectPtr<const UAnalytics_TelemetryDataAsset> CachedDataAsset;

	/** True once we have attempted to resolve the data asset. */
	bool bDataAssetResolutionAttempted = false;

	/** Resolve (re-resolve) the consent-gated core analytics subsystem. May return null. */
	UAnalytics_Subsystem* ResolveAnalyticsSubsystem();

	/** Resolve (and cache) the telemetry data asset, or null. */
	const UAnalytics_TelemetryDataAsset* ResolveDataAsset();

	/** Subscribe to the configured breadcrumb bus channel (if a bus and a valid channel exist). */
	void SubscribeToBus();

	/** Bus handler: leave a crumb carrying the source channel. */
	void HandleBusMessage(const FDP_Message& Message);

	/** Trim the ring to the configured size, dropping oldest. */
	void EnforceRingCap();
};
