// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "Analytics_ProgressionDataAsset.generated.h"

/**
 * One ordered step in an onboarding / progression funnel.
 *
 * A funnel is an ORDERED list of steps; analytics cares about how far players get. The component
 * records a step the first time it is reached and (optionally) treats it as a milestone. Order is
 * the array order in the owning data asset, so designers reorder steps by dragging, not by editing
 * indices in code.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSANALYTICS_API FAnalytics_FunnelStep
{
	GENERATED_BODY()

	/** Stable identity of the step (e.g. "Analytics.Funnel.Onboarding.PickedClass"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Funnel")
	FGameplayTag StepTag;

	/**
	 * When true, reaching this step also fires OnMilestoneReached and records a milestone event in
	 * addition to the funnel-step event. Use for the small set of steps a project treats as
	 * headline milestones (tutorial complete, first boss, etc.).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Funnel")
	bool bIsMilestone = false;

	/** Optional designer label for tooling; not used at runtime. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Funnel")
	FText DisplayName;

	FAnalytics_FunnelStep() = default;

	bool IsValidStep() const { return StepTag.IsValid(); }
};

/**
 * Data-driven definition of a progression funnel: the ordered set of steps a player can reach and
 * which of them are milestones. Identity (DataTag) is the funnel id a progression component points
 * at. Everything is data so there are no hard-coded step names or counts in code.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSANALYTICS_API UAnalytics_ProgressionDataAsset : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/** The funnel's ordered steps. The component records the highest step index reached as depth. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Funnel")
	TArray<FAnalytics_FunnelStep> Steps;

	/**
	 * Seconds of accumulated playtime between automatic playtime-heartbeat events. <= 0 disables
	 * heartbeats (playtime is still accumulated and emitted in the session summary). Data, not magic.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Playtime", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float PlaytimeHeartbeatSeconds = 300.0f;

	/** Find a step by tag; returns INDEX_NONE if the funnel has no such step. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Funnel")
	int32 IndexOfStep(FGameplayTag StepTag) const;

	/** True if StepTag is declared in this funnel. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Funnel")
	bool ContainsStep(FGameplayTag StepTag) const;

	/** True if the named step is flagged as a milestone (false if unknown). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Funnel")
	bool IsMilestoneStep(FGameplayTag StepTag) const;

#if WITH_EDITOR
	//~ Begin UObject (editor validation)
	/** Flags empty/duplicate step tags. */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
	//~ End UObject
#endif
};
