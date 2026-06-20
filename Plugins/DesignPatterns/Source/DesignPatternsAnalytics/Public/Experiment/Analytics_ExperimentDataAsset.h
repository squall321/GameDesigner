// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "Analytics_ExperimentDataAsset.generated.h"

/**
 * One assignable bucket of an A/B experiment.
 *
 * RolloutWeight is a relative (NOT necessarily summing-to-one) weight. The experiment
 * subsystem normalises the weights of all enabled variants at assignment time, so a
 * designer can express "10% / 90%" as {1, 9} or {10, 90} interchangeably and can disable
 * a variant by setting its weight to 0 without re-balancing the others.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSANALYTICS_API FAnalytics_ExperimentVariant
{
	GENERATED_BODY()

	/**
	 * Stable identity of this variant (e.g. "Analytics.Experiment.Onboarding.Variant.Long").
	 * Returned verbatim by UAnalytics_ExperimentSubsystem::GetVariant and recorded as the
	 * assignment attribute, so it must be stable across builds for the data to stay comparable.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Experiment")
	FGameplayTag VariantTag;

	/**
	 * Relative rollout weight. Must be >= 0. A variant with weight 0 is treated as disabled
	 * and is never assigned (it is skipped during normalisation). The set of weights across
	 * variants defines the proportion of players bucketed into each variant.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Experiment", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float RolloutWeight = 1.0f;

	FAnalytics_ExperimentVariant() = default;

	FAnalytics_ExperimentVariant(const FGameplayTag& InVariantTag, float InWeight)
		: VariantTag(InVariantTag)
		, RolloutWeight(InWeight)
	{
	}

	/** A variant participates in assignment only when it has a valid tag and a strictly positive weight. */
	bool IsAssignable() const { return VariantTag.IsValid() && RolloutWeight > 0.0f; }
};

/**
 * Data-driven definition of a single A/B experiment (or staged feature rollout).
 *
 * Identity (DataTag, inherited from UDP_DataAsset) IS the experiment tag — game code asks the
 * experiment subsystem for a variant by that tag and the subsystem finds this asset through the
 * data registry. Everything about the rollout (which variants exist, their weights, the default,
 * whether the experiment is live) is authored here as data; there are no hard-coded percentages
 * or variant names in code.
 *
 * Determinism contract: the subsystem assigns a player to a variant by a stable hash of
 * (player id + experiment tag), so a given player always sees the same variant for the life of
 * the asset's variant set. Re-weighting or adding variants intentionally CAN re-bucket players;
 * bump the asset's VariantSetVersion when you want that to be an explicit, auditable change.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSANALYTICS_API UAnalytics_ExperimentDataAsset : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/**
	 * Master on/off for the whole experiment. When false the subsystem behaves as if the
	 * experiment did not exist: GetVariant returns the default variant and records nothing,
	 * and IsFeatureEnabled falls through to bDefaultFeatureEnabled. Use this to ship an
	 * experiment dormant and flip it on remotely (via a data hotfix) without a code change.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Experiment")
	bool bExperimentEnabled = true;

	/**
	 * The candidate variants. Weights are relative and normalised at assignment time. An empty
	 * or all-zero-weight list is valid and means "always the default variant" (a safe inert
	 * state). Duplicate VariantTags are flagged by editor validation.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Experiment")
	TArray<FAnalytics_ExperimentVariant> Variants;

	/**
	 * The variant returned when the experiment is disabled, has no assignable variants, or a
	 * stable player id is unavailable (so assignment cannot be deterministic). This is the
	 * documented inert fallback and SHOULD correspond to the shipped/control behaviour.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Experiment")
	FGameplayTag DefaultVariantTag;

	/**
	 * Interpretation of this experiment as a boolean feature flag. When the player's assigned
	 * variant tag is present in this container, IsFeatureEnabled returns true. This lets one
	 * experiment asset drive a simple on/off gate as well as multi-arm bucketing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Experiment|Feature Flag")
	FGameplayTagContainer FeatureEnabledForVariants;

	/**
	 * Value of IsFeatureEnabled when the experiment is disabled or no variant could be assigned.
	 * Keep this matching the control/shipped behaviour so a disabled experiment is inert.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Experiment|Feature Flag")
	bool bDefaultFeatureEnabled = false;

	/**
	 * Monotonic version of the variant SET. It is mixed into the assignment hash so that
	 * incrementing it deliberately re-buckets every player (e.g. after a meaningful re-weight),
	 * while leaving it unchanged keeps assignments sticky. Stored as data so re-bucketing is an
	 * auditable content change, never an implicit side effect of editing weights.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Experiment", meta = (ClampMin = "0"))
	int32 VariantSetVersion = 0;

	/** Sum of the weights of all assignable variants. 0 when there are none (use the default). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Experiment")
	float GetTotalAssignableWeight() const;

	/** True if at least one variant is assignable (valid tag, positive weight). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Experiment")
	bool HasAssignableVariants() const;

	/**
	 * Pick a variant deterministically from a normalised selector in [0,1). Walks the assignable
	 * variants in declared order accumulating normalised weight and returns the bucket the selector
	 * falls into. Returns DefaultVariantTag when there are no assignable variants. Pure given the
	 * asset's data, so identical (Selector) inputs always yield the same variant.
	 *
	 * @param NormalizedSelector A value in [0,1); values are clamped into range defensively.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Experiment")
	FGameplayTag SelectVariantFromNormalized(float NormalizedSelector) const;

	/** True if VariantTag is one of this experiment's declared variants (assignable or not). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Experiment")
	bool IsKnownVariant(FGameplayTag VariantTag) const;

#if WITH_EDITOR
	//~ Begin UObject (editor validation)
	/** Flags duplicate variant tags, negative weights, and a default that is not a declared variant. */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
	//~ End UObject
#endif
};
