// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "UObject/ScriptInterface.h"
#include "Analytics_EconomyTelemetryComponent.generated.h"

class UAnalytics_Subsystem;
class ISeam_Wallet;

/**
 * Player-owned, strictly LOCAL economy sink/source telemetry component.
 *
 * What it measures (all local/per-machine; nothing replicates or saves into gameplay state):
 *  - RecordResourceFlow(resource, delta, sourceSink) accumulates net flow per resource and per
 *    (resource, source/sink) pair and records an Analytics.Event.Economy.Flow event (delta as a
 *    FSeam_NetValue int; resource + reason as Tag attributes) through the consent-gated core
 *    subsystem. Inert (still accumulates locally) when no core subsystem is available.
 *  - FlushEconomySnapshot emits an aggregate Analytics.Event.Economy.Snapshot (net flow per
 *    resource) and, when a wallet seam is supplied, the current balances too.
 *
 * Optional auto-sampling: a host may supply an ISeam_Wallet via SetWalletProvider (a host-supplied
 * TScriptInterface — NEVER a concrete economy/genre include) so FlushEconomySnapshot can also record
 * live balances. Without it the component records only explicit flows (a documented inert fallback).
 *
 * No tick, no replication: the constructor disables ticking and replication and the UCLASS hides the
 * replication categories, mirroring UAnalytics_ProgressionComponent. Attach to the player controller
 * / player state / pawn whose economy you want to measure.
 */
UCLASS(ClassGroup = (DesignPatterns), meta = (BlueprintSpawnableComponent),
	HideCategories = (Variable, Sockets, Tags, ComponentReplication, Activation, Cooking, AssetUserData, Collision))
class DESIGNPATTERNSANALYTICS_API UAnalytics_EconomyTelemetryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAnalytics_EconomyTelemetryComponent();

	//~ Begin UActorComponent
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent

	/**
	 * Record a single resource flow. Delta > 0 is a source (gain), Delta < 0 is a sink (spend). The
	 * net total per resource and the per-(resource, source/sink) total are accumulated locally, and an
	 * Economy.Flow event is recorded through the consent-gated core subsystem.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Analytics|Economy")
	void RecordResourceFlow(FGameplayTag ResourceTag, int64 Delta, FGameplayTag SourceSinkTag);

	/** Net accumulated flow for a resource (sum of all deltas; 0 if unseen). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Analytics|Economy")
	int64 GetNetFlow(FGameplayTag ResourceTag) const;

	/** Accumulated flow attributed to a specific (resource, source/sink) pair (0 if unseen). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Analytics|Economy")
	int64 GetSourceTotal(FGameplayTag ResourceTag, FGameplayTag SourceSinkTag) const;

	/**
	 * Emit an aggregate economy snapshot: net flow per resource (and live wallet balances when a
	 * wallet provider is set) through the consent-gated core subsystem. Called automatically on
	 * EndPlay; exposed for an explicit checkpoint too.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Analytics|Economy")
	void FlushEconomySnapshot();

	/**
	 * Supply an OPTIONAL read-only wallet seam for balance auto-sampling. Held as a plain
	 * TScriptInterface owned by the host (the component does not keep it alive). Pass an empty
	 * interface to clear. Never include a concrete economy header — only this seam.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Analytics|Economy")
	void SetWalletProvider(const TScriptInterface<ISeam_Wallet>& InWallet);

protected:
	/** Resolve the consent-gated core analytics subsystem from this actor's GameInstance. */
	UAnalytics_Subsystem* ResolveAnalyticsSubsystem();

private:
	/** Net flow accumulated per resource tag. */
	UPROPERTY(Transient)
	TMap<FGameplayTag, int64> NetFlowByResource;

	/** Flow accumulated per (resource, source/sink) pair, keyed by a combined name. */
	UPROPERTY(Transient)
	TMap<FName, int64> FlowByResourceSource;

	/**
	 * Optional host-supplied wallet seam for balance sampling. A TScriptInterface is acceptable here
	 * (not a weak ref) because the host owns the provider's lifetime and the component is a per-actor
	 * object torn down with its owner; we still null-check the object before every use.
	 */
	UPROPERTY(Transient)
	TScriptInterface<ISeam_Wallet> WalletProvider;

	/** Weakly-cached core analytics subsystem (re-resolved if it goes away). */
	UPROPERTY(Transient)
	TWeakObjectPtr<UAnalytics_Subsystem> CachedAnalyticsSubsystem;

	/** Build the combined map key for a (resource, source/sink) pair. */
	static FName MakeResourceSourceKey(const FGameplayTag& Resource, const FGameplayTag& SourceSink);
};
