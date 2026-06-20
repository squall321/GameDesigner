// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
// Core-area (intra-module) headers: the consent-gated recording subsystem and the player-id seam
// this area assigns variants against. Both are produced by the Analytics "core" area of this module.
#include "Subsystem/Analytics_Subsystem.h"
#include "Seam/Analytics_PlayerIdProvider.h"
#include "Analytics_ExperimentSubsystem.generated.h"

class UAnalytics_ExperimentDataAsset;
class UDP_DataRegistrySubsystem;
class UDP_ServiceLocatorSubsystem;

/**
 * One cached, sticky variant assignment for a single experiment.
 *
 * Assignments are computed once per (player id, experiment, variant-set version) and then reused
 * for the rest of the session, so GetVariant is cheap and stable. The cache is keyed by experiment
 * tag; an entry is invalidated if the resolving player id changes (e.g. sign-in completes after a
 * guest session) so the next GetVariant re-buckets against the now-known id.
 */
USTRUCT()
struct FAnalytics_VariantAssignment
{
	GENERATED_BODY()

	/** The variant the player was bucketed into. */
	UPROPERTY()
	FGameplayTag VariantTag;

	/** Player id used for the hash, so we can detect an id change and re-assign. */
	UPROPERTY()
	FString PlayerIdUsed;

	/** Variant-set version the assignment was computed against (re-bucket on bump). */
	UPROPERTY()
	int32 VariantSetVersion = 0;

	/** True once the assignment has been recorded to analytics (record only once per assignment). */
	UPROPERTY()
	bool bRecorded = false;

	/** False until first computed. */
	UPROPERTY()
	bool bValid = false;
};

/**
 * GameInstance subsystem that resolves A/B experiment variants and boolean feature flags.
 *
 * Assignment is DETERMINISTIC and STICKY: a player is bucketed into a variant by a stable 64-bit
 * hash of (stable player id + experiment tag + variant-set version) folded to a [0,1) selector and
 * compared against the experiment's normalised rollout weights. The same player therefore always
 * sees the same variant for a given variant-set version, with no server round-trip and no stored
 * state required — the hash IS the storage. The first time a variant is resolved it is recorded as
 * an analytics event through the consent-gated Analytics core subsystem.
 *
 * Removability / inert default (HARD RULE 10): if the player-id provider seam is unresolved (no
 * stable id), or the experiment asset is missing/disabled, GetVariant returns the asset's default
 * variant (or an empty tag) and IsFeatureEnabled falls through to the asset default — the feature
 * "falls through" exactly as required, recording nothing.
 *
 * GC safety (HARD RULE 3): this is a GameInstance subsystem that references an interface provider
 * across worlds, so the player-id provider is held WEAK (a weak UObject re-resolved/pruned on use,
 * never a hard TScriptInterface, which would outlive worlds and crash).
 */
UCLASS()
class DESIGNPATTERNSANALYTICS_API UAnalytics_ExperimentSubsystem : public UDP_GameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/**
	 * Resolve (and cache) the variant this player is assigned to for Experiment.
	 *
	 * Deterministic and sticky for the life of the variant set. On the FIRST resolution of a given
	 * experiment this also records an "Analytics.Event.Experiment.Assigned" event (experiment +
	 * variant + a coarse, non-PII player-bucket attribute) through the consent-gated core subsystem.
	 *
	 * @param Experiment The experiment's identity tag (== the data asset's DataTag).
	 * @return The assigned variant tag, or the asset's DefaultVariantTag when assignment is not
	 *         possible (asset missing/disabled, or no stable player id). Never asserts.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Analytics|Experiment")
	FGameplayTag GetVariant(FGameplayTag Experiment);

	/**
	 * Boolean feature-flag view of an experiment: true iff the player's assigned variant is listed
	 * in the asset's FeatureEnabledForVariants. Falls through to the asset's bDefaultFeatureEnabled
	 * when the experiment is disabled or no variant could be assigned. Safe to call every frame
	 * (assignment is cached).
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Analytics|Experiment")
	bool IsFeatureEnabled(FGameplayTag Experiment);

	/** True if Experiment resolves to a loaded experiment data asset (no consent / id requirement). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Analytics|Experiment")
	bool IsExperimentKnown(FGameplayTag Experiment) const;

	/**
	 * Drop all cached assignments. The next GetVariant re-buckets from scratch. Intended for tests
	 * and for the rare case a project wants to force re-evaluation after a runtime data hotfix.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Analytics|Experiment")
	void ResetAssignments();

	/**
	 * Compute the deterministic [0,1) selector for (PlayerId, Experiment, VariantSetVersion) WITHOUT
	 * touching the cache or recording anything. Exposed so tests can prove determinism/distribution.
	 */
	static float ComputeSelector(const FString& PlayerId, FGameplayTag Experiment, int32 VariantSetVersion);

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

private:
	/** Per-experiment sticky assignment cache. */
	UPROPERTY(Transient)
	TMap<FGameplayTag, FAnalytics_VariantAssignment> Assignments;

	/**
	 * Weak, non-owning reference to the last-resolved player-id provider object. A GI subsystem
	 * holding an interface provider across worlds MUST keep it weak (HARD RULE 3); we re-resolve
	 * from the service locator and prune this on use rather than ever storing a hard TScriptInterface.
	 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UObject> PlayerIdProviderObject;

	/**
	 * Read the current stable player id, or empty when no provider is resolved / it has no id.
	 * Re-resolves the provider from the service locator (by the settings' PlayerIdProviderServiceTag)
	 * when the cached weak ref is stale. The seam is a BlueprintNativeEvent, so it is invoked via
	 * IAnalytics_PlayerIdProvider::Execute_GetStablePlayerId.
	 */
	FString GetStablePlayerId();

	/** Load (via the data registry) the experiment asset for Experiment, or null. */
	UAnalytics_ExperimentDataAsset* ResolveExperimentAsset(FGameplayTag Experiment) const;

	/** Compute (and cache) the assignment for Experiment given the current player id. */
	const FAnalytics_VariantAssignment& EnsureAssignment(FGameplayTag Experiment, const UAnalytics_ExperimentDataAsset& Asset);

	/** Record the one-time assignment event through the consent-gated core subsystem. */
	void RecordAssignment(FGameplayTag Experiment, const FAnalytics_VariantAssignment& Assignment);

	/** Cached pointer to the core recording subsystem (same GI; safe to hold strong, same lifetime). */
	UPROPERTY(Transient)
	TWeakObjectPtr<UAnalytics_Subsystem> AnalyticsSubsystem;

	/** Resolve the core analytics subsystem (consent gate + sink wrapper) from this GI, null-safe. */
	UAnalytics_Subsystem* ResolveAnalyticsSubsystem();
};
