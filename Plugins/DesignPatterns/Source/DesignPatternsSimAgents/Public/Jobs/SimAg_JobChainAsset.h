// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "SimAg_JobChainAsset.generated.h"

/**
 * One step of a multi-stage job chain (e.g. "mine ore" -> "haul ore to smelter" -> "smelt"). Each step
 * is genre-neutral: kinds, skills and resources are tags. A step becomes eligible once every tag in
 * Prerequisites has been completed.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMAGENTS_API FSimAg_JobStep
{
	GENERATED_BODY()

	/** Kind of work this step represents (matched against the job board / strategy JobKind). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Jobs")
	FGameplayTag JobKind;

	/** Optional skill the agent must advertise to perform this step (empty = anyone). Matched by hierarchy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Jobs")
	FGameplayTag RequiredSkill;

	/**
	 * Step kinds that must already be completed before this one is eligible (dependencies / prereqs).
	 * Empty = no prerequisites (a starting step).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Jobs")
	FGameplayTagContainer Prerequisites;

	/** Optional resource this step consumes (a hauling source / input). Empty if none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Jobs")
	FGameplayTag InputResource;

	/** Optional resource this step produces (a hauling destination / output). Empty if none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Jobs")
	FGameplayTag OutputResource;

	/**
	 * Need whose urgency raises this step's priority (priority-by-need hint). E.g. a "cook food" step is
	 * more urgent when the colony's Hunger need is low. Empty = no need coupling. Consumed by the chained
	 * job strategy; no magic numbers here.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Jobs", meta = (Categories = "SimAg.Need"))
	FGameplayTag PriorityNeed;

	/** Base relative importance of this step (designer-authored; combined with PriorityNeed urgency). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Jobs", meta = (ClampMin = "0.0"))
	float BasePriority = 1.f;

	FSimAg_JobStep() = default;

	/** True when every prerequisite kind is present in CompletedKinds. */
	bool IsEligible(const FGameplayTagContainer& CompletedKinds) const
	{
		return Prerequisites.IsEmpty() || CompletedKinds.HasAll(Prerequisites);
	}
};

/**
 * A tag-identified job-chain asset: an ordered list of FSimAg_JobStep describing a dependency graph of
 * work (mine -> haul -> craft). Authored once and shared; resolved by tag through the core data registry
 * like USimAg_ScheduleAsset. Pure data, genre-neutral.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSSIMAGENTS_API USimAg_JobChainAsset : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	USimAg_JobChainAsset();

	/** Ordered steps. Order is the authoring default tie-break; Prerequisites express the real graph. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Jobs")
	TArray<FSimAg_JobStep> Steps;

	/**
	 * The first step (in array order) whose prerequisites are all satisfied by CompletedKinds and that is
	 * not itself already completed. Returns a default (empty JobKind) step when none is eligible (chain
	 * finished or blocked).
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimAgents|Jobs")
	FSimAg_JobStep GetNextEligibleStep(const FGameplayTagContainer& CompletedKinds) const;

	/** True if every step's kind is present in CompletedKinds (the whole chain is done). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimAgents|Jobs")
	bool IsChainComplete(const FGameplayTagContainer& CompletedKinds) const;

	//~ Begin UDP_DataAsset
	/** Group all job-chain assets under one asset-manager type bucket. */
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset

#if WITH_EDITOR
	//~ Begin UObject (editor validation)
	/** Warns on steps with no JobKind and prerequisites referencing kinds no step provides. */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
	//~ End UObject
#endif
};
